#pragma once
#include "node_config.h"

#ifdef ENABLE_MPU6050
#include "bus.h"

// MPU-6050 shares I2C with LCD by default. Override in node config if needed.
#ifndef MPU_SDA_PIN
#define MPU_SDA_PIN LCD_SDA_PIN
#endif
#ifndef MPU_SCL_PIN
#define MPU_SCL_PIN LCD_SCL_PIN
#endif

void mpu_setup();
void mpu_loop();

#endif // ENABLE_MPU6050
