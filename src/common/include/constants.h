#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <stdint.h>

// ====================
// I2C Bus Configuration
// ====================
#define I2C_DEVICE_PATH "/dev/i2c1"

// ====================
// OLED Display Constants
// ====================
#define SSD1306_I2C_ADDR 0x3C

// ====================
// PCA9685 I2C Address and Registers
// ====================
#define PCA9685_ADDR 0x5F

// PCA9685 Registers
#define PCA9685_MODE1 0x00
#define PCA9685_PRESCALE 0xFE
#define PCA9685_LED0_ON_L 0x06

// PCA9685 PWM Configuration
#define PCA9685_PWM_FREQUENCY 50  // 50Hz for motor control
#define PCA9685_PWM_RESOLUTION 4096  // 12-bit resolution

// ====================
// Motor Pin Mappings (PCA9685 Channels)
// ====================
// Motor 1 (Front Right)
#define M1_IN1 15
#define M1_IN2 14

// Motor 2 (Front Left)
#define M2_IN1 12
#define M2_IN2 13

// Motor 3 (Rear Right)
#define M3_IN1 11
#define M3_IN2 10

// Motor 4 (Rear Left)
#define M4_IN1 8
#define M4_IN2 9

// ====================
// Motor Control Limits
// ====================
#define MOTOR_SPEED_MIN -1.0f
#define MOTOR_SPEED_MAX 1.0f
#define MOTOR_SPEED_STOP 0.0f

#endif // CONSTANTS_H
