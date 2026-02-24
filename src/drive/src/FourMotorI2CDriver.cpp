#include "../include/FourMotorI2CDriver.hpp"
#include "../../common/include/constants.h"
#include "../../common/include/ipc.h"

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <devctl.h>
#include <hw/i2c.h>
#include <math.h>
#include <stdexcept>
#include <cstring>

FourMotorI2CDriver::FourMotorI2CDriver() 
    : hw_lock_(nullptr), i2c_fd_(-1), initialized_(false) {
    
    hw_lock_ = name_attach(NULL, IPC_HW_LOCK_NAME, 0);
    if (hw_lock_ == nullptr) {
        throw std::runtime_error(
            "Hardware lock already held by another process. "
            "Only one motor controller can run at a time."
        );
    }
    
    i2c_fd_ = open(I2C_DEVICE_PATH, O_RDWR);
    if (i2c_fd_ < 0) {
        name_detach(hw_lock_, 0);
        throw std::runtime_error(
            std::string("Failed to open I2C device: ") + I2C_DEVICE_PATH + 
            ". Error: " + strerror(errno)
        );
    }
    
    std::cout << "[FourMotorI2CDriver] Hardware lock acquired, I2C opened." << std::endl;
}

FourMotorI2CDriver::~FourMotorI2CDriver() {
    std::cout << "[FourMotorI2CDriver] Destructor: Stopping motors and releasing hardware..." << std::endl;
    
    // Stop motors safely - destructors must not throw exceptions
    try {
        stopAll();
    }
    catch (const std::exception& e) {
        std::cerr << "[FourMotorI2CDriver] WARNING: Failed to stop motors during cleanup: " 
                  << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "[FourMotorI2CDriver] WARNING: Unknown error stopping motors during cleanup." << std::endl;
    }
    
    if (i2c_fd_ >= 0) {
        close(i2c_fd_);
        i2c_fd_ = -1;
    }
    
    if (hw_lock_ != nullptr) {
        name_detach(hw_lock_, 0);
        hw_lock_ = nullptr;
    }
    
    std::cout << "[FourMotorI2CDriver] Cleanup complete." << std::endl;
}

bool FourMotorI2CDriver::init() {
    if (i2c_fd_ < 0) {
        std::cerr << "[FourMotorI2CDriver] ERROR: I2C device not open." << std::endl;
        return false;
    }
    
    try {
        writeRegister(PCA9685_MODE1, 0x00);
        usleep(5000);
        
        // PCA9685 prescale formula: round(25MHz / (4096 * freq)) - 1
        float prescale_val = 25000000.0f / (4096.0f * PCA9685_PWM_FREQUENCY);
        uint8_t prescale = (uint8_t)(round(prescale_val) - 1);
        
        // Put chip to sleep to set prescale, then wake and enable auto-increment
        uint8_t oldmode = readRegister(PCA9685_MODE1);
        uint8_t newmode = (oldmode & 0x7F) | 0x10;
        
        writeRegister(PCA9685_MODE1, newmode);
        writeRegister(PCA9685_PRESCALE, prescale);
        writeRegister(PCA9685_MODE1, oldmode);
        usleep(5000);
        writeRegister(PCA9685_MODE1, oldmode | 0xa1);
        
        initialized_ = true;
        std::cout << "[FourMotorI2CDriver] PCA9685 initialized at " 
                  << PCA9685_PWM_FREQUENCY << "Hz" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[FourMotorI2CDriver] Initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void FourMotorI2CDriver::setSpeeds(float left_speed, float right_speed) {
    if (!initialized_) {
        std::cerr << "[FourMotorI2CDriver] WARNING: Not initialized, ignoring command." << std::endl;
        return;
    }
    
    if (left_speed > MOTOR_SPEED_MAX) left_speed = MOTOR_SPEED_MAX;
    if (left_speed < MOTOR_SPEED_MIN) left_speed = MOTOR_SPEED_MIN;
    if (right_speed > MOTOR_SPEED_MAX) right_speed = MOTOR_SPEED_MAX;
    if (right_speed < MOTOR_SPEED_MIN) right_speed = MOTOR_SPEED_MIN;
    
    setMotorSpeed(1, right_speed);
    setMotorSpeed(2, left_speed);
    setMotorSpeed(3, right_speed);
    setMotorSpeed(4, left_speed);
}

void FourMotorI2CDriver::stopAll() {
    if (i2c_fd_ < 0) return;
    
    setMotorSpeed(1, 0.0f);
    setMotorSpeed(2, 0.0f);
    setMotorSpeed(3, 0.0f);
    setMotorSpeed(4, 0.0f);
}

void FourMotorI2CDriver::writeRegister(uint8_t reg, uint8_t value) {
    struct {
        i2c_send_t hdr;
        uint8_t data[2];
    } msg;
    
    msg.hdr.slave.addr = PCA9685_ADDR;
    msg.hdr.slave.fmt = I2C_ADDRFMT_7BIT;
    msg.hdr.len = 2;
    msg.hdr.stop = 1;
    
    msg.data[0] = reg;
    msg.data[1] = value;
    
    int result = devctl(i2c_fd_, DCMD_I2C_SEND, &msg, sizeof(msg), NULL);
    if (result != EOK) {
        throw std::runtime_error("I2C write failed");
    }
}

uint8_t FourMotorI2CDriver::readRegister(uint8_t reg) {
    struct {
        i2c_sendrecv_t hdr;
        uint8_t data[1];
    } msg;
    
    msg.hdr.slave.addr = PCA9685_ADDR;
    msg.hdr.slave.fmt = I2C_ADDRFMT_7BIT;
    msg.hdr.send_len = 1;
    msg.hdr.recv_len = 1;
    msg.hdr.stop = 1;
    
    msg.data[0] = reg;
    
    int result = devctl(i2c_fd_, DCMD_I2C_SENDRECV, &msg, sizeof(msg), NULL);
    if (result != EOK) {
        throw std::runtime_error("I2C read failed");
    }
    
    return msg.data[0];
}

void FourMotorI2CDriver::setPWM(uint8_t channel, uint16_t on_val, uint16_t off_val) {
    struct {
        i2c_send_t hdr;
        uint8_t data[5];
    } msg;
    
    msg.hdr.slave.addr = PCA9685_ADDR;
    msg.hdr.slave.fmt = I2C_ADDRFMT_7BIT;
    msg.hdr.len = 5;
    msg.hdr.stop = 1;
    
    msg.data[0] = PCA9685_LED0_ON_L + (4 * channel);
    msg.data[1] = (uint8_t)(on_val & 0xFF);
    msg.data[2] = (uint8_t)(on_val >> 8);
    msg.data[3] = (uint8_t)(off_val & 0xFF);
    msg.data[4] = (uint8_t)(off_val >> 8);
    
    int result = devctl(i2c_fd_, DCMD_I2C_SEND, &msg, sizeof(msg), NULL);
    if (result != EOK) {
        // Periodic timer will retry on next cycle
        std::cerr << "[FourMotorI2CDriver] WARNING: I2C PWM write failed for channel " 
                  << (int)channel << " (result=" << result << ")" << std::endl;
    }
}

void FourMotorI2CDriver::setMotorSpeed(int motor_num, float speed) {
    int in1_channel, in2_channel;
    
    switch (motor_num) {
        case 1: in1_channel = M1_IN1; in2_channel = M1_IN2; break;
        case 2: in1_channel = M2_IN1; in2_channel = M2_IN2; break;
        case 3: in1_channel = M3_IN1; in2_channel = M3_IN2; break;
        case 4: in1_channel = M4_IN1; in2_channel = M4_IN2; break;
        default: return;
    }
    
    if (speed > MOTOR_SPEED_MAX) speed = MOTOR_SPEED_MAX;
    if (speed < MOTOR_SPEED_MIN) speed = MOTOR_SPEED_MIN;
    
    if (speed == 0.0f) {
        setPWM(in1_channel, 0, 0);
        setPWM(in2_channel, 0, 0);
    } 
    else if (speed > 0.0f) {
        // Disable reverse before engaging forward
        setPWM(in2_channel, 0, 0);
        uint16_t pwm_val = (uint16_t)(speed * (PCA9685_PWM_RESOLUTION - 1));
        setPWM(in1_channel, 0, pwm_val);
    } 
    else {
        // Disable forward before engaging reverse
        setPWM(in1_channel, 0, 0);
        uint16_t pwm_val = (uint16_t)(-speed * (PCA9685_PWM_RESOLUTION - 1));
        setPWM(in2_channel, 0, pwm_val);
    }
}
