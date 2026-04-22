// mod_viper.cpp — Viper 5305V serial bridge.
// Receives VIPER_CMD frames from the bus and executes lock/unlock/remote-start
// over UART2. Broadcasts VIPER_STATUS when the alarm responds.
//
// The ViperESP2 class is inlined here (originally a separate library file).
// Protocol: 5-byte frames at 9600 baud, 8N1. Checksum = 0xFF - sum(bytes 0-3).
//   Lock/Arm:       01 01 01 01 FE
//   Unlock/Disarm:  01 02 01 01 FD
//   Remote Start:   01 03 01 01 FC
//   Panic:          01 04 01 01 FB

#include <Arduino.h>
#include "node_config.h"

#ifdef ENABLE_VIPER

#include "can_protocol.h"
#include "bus.h"
#include "mod_lcd.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

// --------------------------------------------------------------
// ViperESP2 — inline implementation
// --------------------------------------------------------------
class ViperESP2 {
public:
  ViperESP2(HardwareSerial& s) : _s(s) {}

  void begin() { _s.begin(9600, SERIAL_8N1, VIPER_RX_PIN, VIPER_TX_PIN); }

  void update() {
    while (_s.available()) {
      _buf[_pos++] = _s.read();
      if (_pos == 5) { if (_cb) _cb(_buf, 5); _pos = 0; }
    }
  }

  void onMessage(void (*cb)(uint8_t*, int)) { _cb = cb; }

  void lock()        { uint8_t c[] = {0x01,0x01,0x01,0x01,0xFE}; _s.write(c, 5); }
  void unlock()      { uint8_t c[] = {0x01,0x02,0x01,0x01,0xFD}; _s.write(c, 5); }
  void remoteStart() { uint8_t c[] = {0x01,0x03,0x01,0x01,0xFC}; _s.write(c, 5); }

private:
  HardwareSerial& _s;
  void (*_cb)(uint8_t*, int) = nullptr;
  uint8_t _buf[5];
  int _pos = 0;
};

static ViperESP2 g_viper(Serial2);

static void on_viper_message(uint8_t* buf, int len) {
  uint8_t pkt[5] = {};
  memcpy(pkt, buf, len > 5 ? 5 : len);
  bus_tx(CAN_ID_VIPER_STATUS, pkt, 5);
  wlog("[viper<-] %02X %02X %02X %02X %02X\n",
       pkt[0], pkt[1], pkt[2], pkt[3], pkt[4]);
  lcd_set_event("Viper: Response");
}

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------
void viper_setup() {
  g_viper.onMessage(on_viper_message);
  g_viper.begin();
  wlogln("[viper] UART2 ready");
}

void viper_loop() {
  g_viper.update();
}

void viper_handle_frame(const BusFrame& f) {
  if (f.id != CAN_ID_VIPER_CMD || f.dlc < 1) return;
  switch (f.data[0]) {
    case VIPER_CMD_LOCK:
      g_viper.lock();
      wlog("[viper->] lock (via %s)\n", f.source);
      break;
    case VIPER_CMD_UNLOCK:
      g_viper.unlock();
      wlog("[viper->] unlock (via %s)\n", f.source);
      break;
    case VIPER_CMD_REMOTE_START:
      g_viper.remoteStart();
      wlog("[viper->] remote-start (via %s)\n", f.source);
      break;
    default:
      wlog("[viper->] unknown cmd 0x%02X\n", f.data[0]);
      break;
  }
}

#endif // ENABLE_VIPER
