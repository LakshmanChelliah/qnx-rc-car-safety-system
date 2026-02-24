#ifndef FOURMOTORI2CDRIVER_HPP
#define FOURMOTORI2CDRIVER_HPP

#include <stdint.h>
#include <sys/dispatch.h>

/**
 * FourMotorI2CDriver - Hardware Abstraction Layer
 * 
 * Manages 4 motors via PCA9685 PWM controller over I2C.
 * Implements QNX IPC hardware locking to ensure exclusive access.
 * Uses RAII pattern to guarantee safe cleanup on destruction.
 * 
 * Speed Range: -1.0 (full reverse) to 1.0 (full forward), 0.0 = stop
 */
class FourMotorI2CDriver {
public:
    /**
     * Constructor
     * Attempts to acquire hardware lock and open I2C bus.
     * Throws runtime_error if hardware is already in use or I2C fails.
     */
    FourMotorI2CDriver();
    
    /**
     * Destructor (RAII Safety)
     * Automatically stops all motors, closes I2C, and releases hardware lock.
     */
    ~FourMotorI2CDriver();
    
    // Prevent copying (hardware should only be controlled by one instance)
    FourMotorI2CDriver(const FourMotorI2CDriver&) = delete;
    FourMotorI2CDriver& operator=(const FourMotorI2CDriver&) = delete;
    
    /**
     * Initialize PCA9685 chip
     * Must be called before setSpeeds().
     * Returns true on success, false on failure.
     */
    bool init();
    
    /**
     * Set motor speeds for differential drive
     * @param left_speed: Speed for left motors (M2, M4), range [-1.0, 1.0]
     * @param right_speed: Speed for right motors (M1, M3), range [-1.0, 1.0]
     * 
     * Values are automatically clamped to valid range.
     */
    void setSpeeds(float left_speed, float right_speed);
    
    /**
     * Emergency stop - sets all motors to 0
     */
    void stopAll();

private:
    // QNX IPC Hardware Lock
    name_attach_t* hw_lock_;
    
    // I2C file descriptor
    int i2c_fd_;
    
    // Initialization state
    bool initialized_;
    
    // Low-level I2C communication
    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
    void setPWM(uint8_t channel, uint16_t on_val, uint16_t off_val);
    
    // Motor control helpers
    void setMotorSpeed(int motor_num, float speed);
};

#endif // FOURMOTORI2CDRIVER_HPP
