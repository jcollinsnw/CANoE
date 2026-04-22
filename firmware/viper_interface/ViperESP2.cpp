/* * Viper 5305V ESP2 Serial Bridge for ESP32
 * Note: Use a Level Shifter between ESP32 (3.3V) and Viper (5V)
 * The protocol usually consists of a 5-byte hex string sent at 9600 baud.
 * Frame format: [Header] [Command] [Status] [Checksum]
 * Note: The checksum is typically a simple subtraction:
 *   0xFF - (Byte1 + Byte2 + Byte3 + Byte4).
 * Common Commands (HEX):
 *   Function		Hex Sequence
 *   Arm/Lock		0x01 0x01 0x01 0x01 0xFE
 *   Disarm/Unlock	0x01 0x02 0x01 0x01 0xFD
 *   Remote Start	0x01 0x03 0x01 0x01 0xFC
 *   Panic		0x01 0x04 0x01 0x01 0xFB
 *   Aux 10x01 0x05 0x01 0x01 0xFA
 */
 
#include "ViperESP2.h"

ViperESP2::ViperESP2(HardwareSerial& serial) : _vSerial(serial) {}

void ViperESP2::begin() { _vSerial.begin(9600, SERIAL_8N1, 16, 17); }

void ViperESP2::sniff() {
  if (_vSerial.available()) {
    uint8_t b = _vSerial.read();
    Serial.printf("%02X ", b); // Prints hex to the PC debug console
  }
}

void ViperESP2::onMessage(void (*function)(uint8_t*, int)) { _callback = function; }

void ViperESP2::update() {
  while (_vSerial.available()) {
    uint8_t b = _vSerial.read();
    _buffer[_pos++] = b;
    if (_pos == 5) { // Assuming 5-byte packets
      if (_callback) _callback(_buffer, 5);
      _pos = 0;
    }
  }
}

void ViperESP2::lock() {
  uint8_t cmd[] = {0x01, 0x01, 0x01, 0x01, 0xFE};
  _vSerial.write(cmd, 5);
}
void ViperESP2::unlock() {
  uint8_t cmd[] = {0x01, 0x02, 0x01, 0x01, 0xFD};
  _vSerial.write(cmd, 5);
}
void ViperESP2::remoteStart() {
  uint8_t cmd[] = {0x01, 0x03, 0x01, 0x01, 0xFC};
  _vSerial.write(cmd, 5);
}
