// mod_mpu6050.cpp — MPU-6050 accelerometer/gyro via I2C.
// Broadcasts raw accel on CAN_ID_IMU_DATA every 200 ms.
// Detects shakes (large delta between readings) and broadcasts CAN_ID_SHAKE_EVENT.

#include <Arduino.h>
#include <Wire.h>
#include "node_config.h"

#ifdef ENABLE_MPU6050

#include "can_protocol.h"
#include "bus.h"
#include "mod_lcd.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#define MPU_ADDR         0x68
#define MPU_REG_PWR_MGMT 0x6B
#define MPU_REG_ACCEL    0x3B   // ACCEL_XOUT_H
#define MPU_INTERVAL_MS  200
#define SHAKE_THRESHOLD  8000   // raw delta; ~0.5g at ±2g scale
#define SHAKE_COOLDOWN   2000

static int16_t g_accel[3]      = {};
static int16_t g_accel_prev[3] = {};
static bool    g_mpu_ok        = false;

static void mpu_write_reg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}

static bool mpu_read_accel(int16_t out[3]) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_REG_ACCEL);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6);
  if (Wire.available() < 6) return false;
  for (int i = 0; i < 3; i++) {
    uint8_t hi = Wire.read(), lo = Wire.read();
    out[i] = (int16_t)((hi << 8) | lo);
  }
  return true;
}

void mpu_setup() {
  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
  mpu_write_reg(MPU_REG_PWR_MGMT, 0x00);  // wake up
  delay(10);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75);  // WHO_AM_I
  if (Wire.endTransmission(false) != 0) { wlogln("[mpu] not found"); return; }
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
  if (!Wire.available()) { wlogln("[mpu] no response"); return; }
  uint8_t who = Wire.read();
  if (who != 0x68 && who != 0x72) { wlog("[mpu] unexpected WHO_AM_I=0x%02X\n", who); return; }
  g_mpu_ok = true;
  mpu_read_accel(g_accel_prev);  // seed delta baseline
  wlogln("[mpu] MPU-6050 OK");
}

void mpu_loop() {
  if (!g_mpu_ok) return;
  static uint32_t last_read = 0, last_shake = 0;
  uint32_t now = millis();
  if (now - last_read < MPU_INTERVAL_MS) return;
  last_read = now;

  if (!mpu_read_accel(g_accel)) return;

  // Broadcast raw accel
  uint8_t d[6];
  pack_i16(&d[0], g_accel[0]);
  pack_i16(&d[2], g_accel[1]);
  pack_i16(&d[4], g_accel[2]);
  bus_tx(CAN_ID_IMU_DATA, d, 6);

  // Shake detection
  int32_t max_delta = 0;
  uint8_t axis_mask = 0;
  for (int i = 0; i < 3; i++) {
    int32_t delta = abs((int32_t)g_accel[i] - (int32_t)g_accel_prev[i]);
    if (delta > SHAKE_THRESHOLD) axis_mask |= (1 << i);
    if (delta > max_delta) max_delta = delta;
  }
  memcpy(g_accel_prev, g_accel, sizeof(g_accel));

  if (axis_mask && (now - last_shake) >= SHAKE_COOLDOWN) {
    last_shake = now;
    uint8_t mag = (max_delta > 255 * 128) ? 255 : (uint8_t)(max_delta / 128);
    uint8_t sd[2] = { mag, axis_mask };
    bus_tx(CAN_ID_SHAKE_EVENT, sd, 2);
    wlog("[mpu] SHAKE mag=%u axes=0x%02X\n", mag, axis_mask);
    lcd_set_event("!! SHAKE !!");
  }
}

#endif // ENABLE_MPU6050
