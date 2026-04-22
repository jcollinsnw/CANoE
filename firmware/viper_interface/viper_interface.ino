// viper_interface.ino
// ESP32 CAN node: bridges a Viper 5305V car alarm to the accessory CAN bus.
//
// Hardware:
//   GPIO 5  = CAN TX  (shared bus standard, see bus.h)
//   GPIO 4  = CAN RX  (shared bus standard, see bus.h)
//   GPIO 16 = UART2 TX -> Viper level-shifter TX  (ViperESP2 hardcoded)
//   GPIO 17 = UART2 RX <- Viper level-shifter RX  (ViperESP2 hardcoded)
//
// CAN messages handled:
//   0x100  RELAY_CMD    (in)  mirrors relay state for LCD display
//   0x101  RELAY_STATUS (in)  keeps g_relay_mirror up to date
//   0x500  LCD_CMD      (in)  write text to local LCD (same protocol as switch_panel)
//   0x510  VIPER_CMD    (in)  data[0] = VIPER_CMD_LOCK / UNLOCK / REMOTE_START
//   0x511  VIPER_STATUS (out) data[0..4] = raw 5-byte packet received from alarm
//   0x400  CONFIG_WRITE (in)  CFG_KEY_WIFI_ENABLED — save to NVS + restart
//
// Requires a 3.3V<->5V level shifter between the ESP32 UART pins and the
// Viper serial header. See ViperESP2.cpp for the serial protocol details.

#include <Arduino.h>
#include <Preferences.h>
#include "driver/twai.h"
#include "soc/gpio_struct.h"

#include "can_protocol.h"
#include "bus.h"
#include "ViperESP2.h"

// ==============================================================
// CONFIG — edit these
// ==============================================================

#define USE_CAN_TRANSCEIVER 0
#define USE_WIFI 1           // 0 = CAN-only, no SoftAP, no web UI, no ESP-NOW
#define USE_LCD  1           // 0 = compile without LCD driver

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#include <Wire.h>  // I2C for LCD and/or MPU-6050

#define CAN_TX_PIN GPIO_NUM_5
#define CAN_RX_PIN GPIO_NUM_4

static const char    NODE_NAME[] = "viper-iface";
static const uint8_t NODE_ID     = 0x03;

// ==============================================================
// State
// ==============================================================
static uint8_t     g_relay_mirror  = 0;
static bool        g_can_ok        = true;
static bool        g_lcd_backlight = true;
static Preferences g_prefs;

// ==============================================================
// LCD driver (HD44780 via PCF8574 I2C backpack) — same as switch_panel
// ==============================================================
#if USE_LCD
#define LCD_I2C_ADDR  0x27   // try 0x3F if display stays blank
#define LCD_COLS      16
#define LCD_ROWS      2
#define LCD_SDA_PIN   21
#define LCD_SCL_PIN   22
#define LCD_BL 0x08
#define LCD_EN 0x04
#define LCD_RS 0x01

static void lcd_i2c_write(uint8_t v) {
  Wire.beginTransmission(LCD_I2C_ADDR);
  Wire.write(v | (g_lcd_backlight ? LCD_BL : 0));
  Wire.endTransmission();
}
static void lcd_pulse(uint8_t v) {
  lcd_i2c_write(v | LCD_EN); delayMicroseconds(1);
  lcd_i2c_write(v & ~LCD_EN); delayMicroseconds(50);
}
static void lcd_nibble(uint8_t n, bool rs) {
  lcd_pulse((n << 4) | (rs ? LCD_RS : 0));
}
static void lcd_byte(uint8_t b, bool rs) {
  lcd_nibble(b >> 4, rs); lcd_nibble(b & 0x0F, rs); delayMicroseconds(40);
}
static void lcd_cmd(uint8_t c)  { lcd_byte(c, false); }
static void lcd_data(uint8_t c) { lcd_byte(c, true);  }
static void lcd_set_cursor(uint8_t row, uint8_t col) {
  static const uint8_t ROW_OFF[] = { 0x00, 0x40 };
  lcd_cmd(0x80 | (ROW_OFF[row & 1] + (col & 0x0F)));
}
static void lcd_clear() { lcd_cmd(0x01); delay(2); }
static void lcd_print_n(const char* s, uint8_t len) {
  for (uint8_t i = 0; i < len && s[i]; i++) lcd_data((uint8_t)s[i]);
}
static void lcd_write_row(uint8_t row, const char* text) {
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "%-16s", text);
  lcd_set_cursor(row, 0);
  lcd_print_n(buf, LCD_COLS);
}

static void lcd_update_status() {
  char relays[7];
  for (uint8_t i = 0; i < 6; i++)
    relays[i] = (g_relay_mirror & (1 << i)) ? ('1' + i) : '-';
  relays[6] = '\0';
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "CAN:%-3s [%s]", g_can_ok ? "OK" : "ERR", relays);
  lcd_write_row(0, buf);
}

static void lcd_set_event(const char* msg) {
  lcd_write_row(1, msg);
}

static void lcd_init() {
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  delay(50);
  lcd_nibble(0x03, false); delay(5);
  lcd_nibble(0x03, false); delayMicroseconds(150);
  lcd_nibble(0x03, false);
  lcd_nibble(0x02, false);
  lcd_cmd(0x28);
  lcd_cmd(0x08);
  lcd_clear();
  lcd_cmd(0x06);
  lcd_cmd(0x0C);
  wlogln("[LCD] init OK");
}
#else
// No-op stubs when LCD is compiled out
static void lcd_update_status() {}
static void lcd_set_event(const char*) {}
#endif  // USE_LCD

// ==============================================================
// MPU-6050 accelerometer/gyro (I2C, shares bus with LCD)
// ==============================================================
// CAN_ID_IMU_DATA payload (6 bytes):
//   [0-1] accel_x  int16 LE (raw, ±2g default = 16384 LSB/g)
//   [2-3] accel_y  int16 LE
//   [4-5] accel_z  int16 LE
//
// CAN_ID_SHAKE_EVENT payload (2 bytes):
//   [0] magnitude  uint8 (0-255, capped; units of ~0.01g after scaling)
//   [1] axis_mask  uint8 (bit0=X, bit1=Y, bit2=Z — which axes exceeded threshold)

#define MPU_ADDR          0x68
#define MPU_REG_PWR_MGMT  0x6B
#define MPU_REG_ACCEL     0x3B   // ACCEL_XOUT_H
#define MPU_INTERVAL_MS   200    // raw broadcast every 200 ms
#define SHAKE_THRESHOLD   8000   // raw accel delta; ~0.5g at ±2g scale
#define SHAKE_COOLDOWN_MS 2000   // min time between shake events

static int16_t  g_accel[3]      = {};  // current X, Y, Z
static int16_t  g_accel_prev[3] = {};  // previous reading for delta
static bool     g_mpu_ok        = false;

static void mpu_write_reg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static bool mpu_init() {
  // Wake up (clear SLEEP bit)
  mpu_write_reg(MPU_REG_PWR_MGMT, 0x00);
  delay(10);
  // Verify device responds
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75);  // WHO_AM_I register
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
  if (!Wire.available()) return false;
  uint8_t who = Wire.read();
  return (who == 0x68 || who == 0x72);  // MPU-6050 or MPU-6052
}

static bool mpu_read_accel(int16_t out[3]) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_REG_ACCEL);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6);
  if (Wire.available() < 6) return false;
  for (int i = 0; i < 3; i++) {
    uint8_t hi = Wire.read();
    uint8_t lo = Wire.read();
    out[i] = (int16_t)((hi << 8) | lo);
  }
  return true;
}

static void send_imu_data() {
  uint8_t d[6];
  pack_i16(&d[0], g_accel[0]);
  pack_i16(&d[2], g_accel[1]);
  pack_i16(&d[4], g_accel[2]);
  bus_tx(CAN_ID_IMU_DATA, d, 6);
}

static void send_shake_event(uint8_t magnitude, uint8_t axis_mask) {
  uint8_t d[2] = { magnitude, axis_mask };
  bus_tx(CAN_ID_SHAKE_EVENT, d, 2);
}

static void poll_mpu() {
  if (!g_mpu_ok) return;
  static uint32_t last_read = 0;
  static uint32_t last_shake = 0;
  uint32_t now = millis();
  if (now - last_read < MPU_INTERVAL_MS) return;
  last_read = now;

  if (!mpu_read_accel(g_accel)) return;
  send_imu_data();

  // Shake detection: check delta from previous reading
  int32_t max_delta = 0;
  uint8_t axis_mask = 0;
  for (int i = 0; i < 3; i++) {
    int32_t delta = abs((int32_t)g_accel[i] - (int32_t)g_accel_prev[i]);
    if (delta > SHAKE_THRESHOLD) axis_mask |= (1 << i);
    if (delta > max_delta) max_delta = delta;
  }
  memcpy(g_accel_prev, g_accel, sizeof(g_accel));

  if (axis_mask && (now - last_shake) >= SHAKE_COOLDOWN_MS) {
    last_shake = now;
    uint8_t mag = (max_delta > 255 * 128) ? 255 : (uint8_t)(max_delta / 128);
    send_shake_event(mag, axis_mask);
    wlog("[mpu] SHAKE mag=%u axes=0x%02X\n", mag, axis_mask);
    lcd_set_event("!! SHAKE !!");
  }
}

// ==============================================================
// Config handlers
// ==============================================================
static bool cfg_for_us(uint8_t t) {
  return t == CFG_TARGET_VIPER || t == CFG_TARGET_BROADCAST;
}
static void handle_cfg_write(const BusFrame& f) {
  if (f.dlc < 8 || !cfg_for_us(f.data[0])) return;
  uint8_t key = f.data[1];
  if (key == CFG_KEY_WIFI_ENABLED) {
#if USE_WIFI
    bool en = (f.data[4] != 0);
    g_prefs.begin("viperiface", false);
    g_prefs.putBool("wifi_en", en);
    g_prefs.end();
    wlog("[cfg<-] wifi_en=%u -> restart\n", en);
    delay(100);
    ESP.restart();
#else
    wlogln("[cfg<-] wifi_en ignored (USE_WIFI=0)");
#endif
  }
}

// ==============================================================
// Viper alarm instance (UART2, pins hardcoded in ViperESP2.cpp)
// ==============================================================
static ViperESP2 g_viper(Serial2);

static void on_viper_message(uint8_t* buf, int len) {
  if (len < 1) return;
  uint8_t pkt[5] = {};
  int n = (len > 5) ? 5 : len;
  memcpy(pkt, buf, n);
  bus_tx(CAN_ID_VIPER_STATUS, pkt, n);
  wlog("[viper<-] raw %02X %02X %02X %02X %02X\n",
       pkt[0], pkt[1], pkt[2], pkt[3], pkt[4]);
  lcd_set_event("Viper: Response");
}

// ==============================================================
// CAN setup (wire side)
// ==============================================================
static void setup_can() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NO_ACK);
  g.tx_queue_len = 10; g.rx_queue_len = 20;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) {
    wlogln("[CAN] init failed"); while (true) delay(1000);
  }
#if !USE_CAN_TRANSCEIVER
  // Open-drain via pad register preserves TWAI signal routing through GPIO matrix
  GPIO.pin[CAN_TX_PIN].pad_driver = 1;
  gpio_set_pull_mode(CAN_TX_PIN, GPIO_PULLUP_ONLY);
  wlogln("[CAN] BENCH mode (open-drain TX, NO_ACK)");
#else
  wlogln("[CAN] TRANSCEIVER mode (NO_ACK)");
#endif
}

// ==============================================================
// Arduino lifecycle
// ==============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  wlogln("");
  wlogln("=== Viper Interface boot ===");

  // Load WiFi enable flag before deciding whether to start the radio
  bool wifi_en = true;
#if USE_WIFI
  {
    Preferences p; p.begin("viperiface", true);
    wifi_en = p.getBool("wifi_en", true);
    p.end();
  }
#endif

  setup_can();
  Serial.println("[setup] CAN done");
  Serial.flush();

#if USE_WIFI
  if (wifi_en) {
    Serial.println("[setup] webui_init..."); Serial.flush();
    webui_init(NODE_NAME, NODE_ID);
    Serial.println("[setup] bus_init..."); Serial.flush();
    bus_init(NODE_ID);
  } else {
    wlogln("[boot] WiFi disabled by NVS flag");
    bus_init_no_wifi(NODE_ID);
  }
#else
  Serial.println("[setup] bus_init_no_wifi..."); Serial.flush();
  bus_init_no_wifi(NODE_ID);
#endif
  Serial.println("[setup] bus done"); Serial.flush();

  Serial.println("[setup] viper begin..."); Serial.flush();
  g_viper.onMessage(on_viper_message);
  g_viper.begin();
  Serial.println("[setup] viper done"); Serial.flush();

#if USE_LCD
  lcd_init();
  lcd_update_status();
  lcd_set_event("Viper Interface");
#endif
  Serial.println("[setup] lcd done");

  // I2C bus scan — helps debug LCD address and MPU-6050 presence
  Serial.print("[i2c] scan: ");
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0)
      Serial.printf("0x%02X ", addr);
  }
  Serial.println();

  // MPU-6050 shares I2C with LCD; Wire.begin() already called above
  g_mpu_ok = mpu_init();
  if (g_mpu_ok) {
    mpu_read_accel(g_accel_prev);  // seed previous reading
    wlogln("[mpu] MPU-6050 init OK");
  } else {
    wlogln("[mpu] MPU-6050 not found");
  }

  wlogln("[boot] viper interface ready");
}

void loop() {
#if USE_WIFI
  webui_tick();
#endif
  bus_tick();

  BusFrame f;
  while (bus_rx(f)) {
    switch (f.id) {
      case CAN_ID_RELAY_STATUS:
        if (f.dlc >= 1) { g_relay_mirror = f.data[0]; lcd_update_status(); }
        break;
      case CAN_ID_RELAY_CMD:
        if (f.dlc >= 2) {
          uint8_t mask = f.data[0], state = f.data[1];
          g_relay_mirror = (g_relay_mirror & ~mask) | (state & mask);
          char msg[LCD_COLS + 1];
          if (__builtin_popcount(mask) == 1) {
            uint8_t idx = __builtin_ctz(mask);
            snprintf(msg, sizeof(msg), "Relay %u %s", idx + 1, (state & mask) ? "ON" : "OFF");
          } else if (mask == 0x3F && state == 0) {
            snprintf(msg, sizeof(msg), "All OFF");
          } else {
            snprintf(msg, sizeof(msg), "Relays %02X:%02X", mask, state);
          }
          lcd_set_event(msg);
          lcd_update_status();
        }
        break;
      case CAN_ID_VIPER_CMD:
        if (f.dlc >= 1) {
          const char* action =
            (f.data[0] == VIPER_CMD_LOCK)        ? "Viper: Lock" :
            (f.data[0] == VIPER_CMD_UNLOCK)       ? "Viper: Unlock" :
            (f.data[0] == VIPER_CMD_REMOTE_START) ? "Viper: R.Start" : "Viper: CMD";
          switch (f.data[0]) {
            case VIPER_CMD_LOCK:
              g_viper.lock();
              wlog("[cmd via %s] viper lock\n", f.source);
              break;
            case VIPER_CMD_UNLOCK:
              g_viper.unlock();
              wlog("[cmd via %s] viper unlock\n", f.source);
              break;
            case VIPER_CMD_REMOTE_START:
              g_viper.remoteStart();
              wlog("[cmd via %s] viper remote-start\n", f.source);
              break;
            default:
              wlog("[cmd via %s] viper unknown cmd 0x%02X\n", f.source, f.data[0]);
              break;
          }
          lcd_set_event(action);
        }
        break;
      case CAN_ID_VIPER_STATUS:
        lcd_set_event("Viper: Response");
        break;
#if USE_LCD
      case CAN_ID_LCD_CMD:
        if (f.dlc >= 1) {
          if (f.data[0] == 0xFF) { lcd_clear(); }
          else if (f.data[0] < LCD_ROWS && f.dlc >= 2) {
            lcd_set_cursor(f.data[0], f.data[1]);
            if (f.dlc > 2) lcd_print_n((const char*)&f.data[2], f.dlc - 2);
          }
        }
        break;
#endif
      case CAN_ID_CONFIG_WRITE: handle_cfg_write(f); break;
      default: break;
    }
  }

  g_viper.update();
  poll_mpu();

  // Periodic TWAI health check + bus-off recovery
  static uint32_t last_check = 0;
  uint32_t now = millis();
  if (now - last_check >= 2000) {
    last_check = now;
    twai_status_info_t info;
    if (twai_get_status_info(&info) == ESP_OK) {
      g_can_ok = (info.state == TWAI_STATE_RUNNING);
      wlog("[twai] state=%d tx_err=%u rx_err=%u\n",
           info.state, info.tx_error_counter, info.rx_error_counter);
      if (info.state == TWAI_STATE_BUS_OFF) {
        wlogln("[bus] TWAI BUS_OFF - recovering");
        twai_initiate_recovery();
      }
      lcd_update_status();
    }
  }
}
