#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <devctl.h>
#include <hw/i2c.h>
#include <math.h>

// PCA9685 I2C Address (from the Python code 0x5f)
#define PCA9685_ADDR 0x5F

// PCA9685 Registers
#define PCA9685_MODE1 0x00
#define PCA9685_PRESCALE 0xFE
#define LED0_ON_L 0x06

// Motor Channels (from the Python code)
#define M1_IN1 15
#define M1_IN2 14
#define M2_IN1 12
#define M2_IN2 13
#define M3_IN1 11
#define M3_IN2 10
#define M4_IN1 8
#define M4_IN2 9

int i2c_fd = -1;

// Helper function to write a single byte to a register (QNX Native)
void writeRegister(uint8_t reg, uint8_t value) {
    // QNX requires packaging the I2C header and data into a single aligned struct
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

    devctl(i2c_fd, DCMD_I2C_SEND, &msg, sizeof(msg), NULL);
}

// Helper function to read a single byte from a register (QNX Native)
uint8_t readRegister(uint8_t reg) {
    // QNX uses a SENDRECV message to write the register, then read the response
    struct {
        i2c_sendrecv_t hdr;
        uint8_t data[1]; // Buffer for 1 byte of send, overwritten by 1 byte of recv
    } msg;

    msg.hdr.slave.addr = PCA9685_ADDR;
    msg.hdr.slave.fmt = I2C_ADDRFMT_7BIT;
    msg.hdr.send_len = 1;
    msg.hdr.recv_len = 1;
    msg.hdr.stop = 1;

    msg.data[0] = reg; // Put the register we want to read into the send buffer

    devctl(i2c_fd, DCMD_I2C_SENDRECV, &msg, sizeof(msg), NULL);

    return msg.data[0]; // The received byte overwrites the payload area
}

// Set the PWM duty cycle for a specific channel (0-15)
// on_val and off_val are between 0 and 4095
void setPWM(uint8_t channel, uint16_t on_val, uint16_t off_val) {
    struct {
        i2c_send_t hdr;
        uint8_t data[5];
    } msg;

    msg.hdr.slave.addr = PCA9685_ADDR;
    msg.hdr.slave.fmt = I2C_ADDRFMT_7BIT;
    msg.hdr.len = 5;
    msg.hdr.stop = 1;

    msg.data[0] = LED0_ON_L + (4 * channel);
    msg.data[1] = (uint8_t)(on_val & 0xFF);
    msg.data[2] = (uint8_t)(on_val >> 8);
    msg.data[3] = (uint8_t)(off_val & 0xFF);
    msg.data[4] = (uint8_t)(off_val >> 8);

    devctl(i2c_fd, DCMD_I2C_SEND, &msg, sizeof(msg), NULL);
}

// Initialize the PCA9685 chip
void initPCA9685(int freq) {
    // Reset the chip
    writeRegister(PCA9685_MODE1, 0x00);
    usleep(5000); // Wait 5ms

    // Calculate prescale for target frequency (e.g., 50Hz)
    // Formula: round(25MHz / (4096 * freq)) - 1
    float prescale_val = 25000000.0f / (4096.0f * freq);
    uint8_t prescale = (uint8_t)(round(prescale_val) - 1);

    // To set the prescale, the chip must be put to sleep
    uint8_t oldmode = readRegister(PCA9685_MODE1);
    uint8_t newmode = (oldmode & 0x7F) | 0x10; // sleep bit

    writeRegister(PCA9685_MODE1, newmode);     // go to sleep
    writeRegister(PCA9685_PRESCALE, prescale); // set the prescaler
    writeRegister(PCA9685_MODE1, oldmode);     // wake up
    usleep(5000);
    writeRegister(PCA9685_MODE1, oldmode | 0xa1); // Turn on auto-increment
}

// Set Servo Angle: maps 0-180 degrees to the correct PWM pulse width
void setServoAngle(uint8_t channel, float angle) {
    // Clamp angle between 0 and 180
    if (angle < 0.0f) angle = 0.0f;
    if (angle > 180.0f) angle = 180.0f;

    // Adafruit specs from the Python script:
    float min_pulse = 500.0f;
    float max_pulse = 2400.0f;

    // Calculate the target pulse width in microseconds
    float pulse_us = min_pulse + (angle / 180.0f) * (max_pulse - min_pulse);

    // The PCA9685 runs at 50Hz, so one period is 20,000 microseconds (1 / 50 * 1,000,000 us)
    // The chip has 4096 steps per period.
    // Convert pulse width in microseconds to a step value (0-4095)
    uint16_t off_val = (uint16_t)((pulse_us * 4096.0f) / 20000.0f);

    setPWM(channel, 0, off_val);
}

int main() {
    std::cout << "--- Adeept Robot QNX Driver ---" << std::endl;

    // 1. Open the I2C bus device (usually /dev/i2c1 on the Pi GPIO)
    i2c_fd = open("/dev/i2c1", O_RDWR);
    if (i2c_fd < 0) {
        std::cerr << "ERROR: Could not open /dev/i2c1. Is the I2C driver running?" << std::endl;
        return -1;
    }

    // 2. Initialize the chip at 50Hz
    std::cout << "Initializing PCA9685..." << std::endl;
    initPCA9685(50);

    // 3. Servo Scanning Sequence
    std::cout << "Centering both servos to 90 degrees..." << std::endl;
    setServoAngle(0, 90); // Base heading
    setServoAngle(1, 90); // Pitch
    sleep(2);

    // Scan Base (Channel 0) Left to Right: 20 to 160
    std::cout << "Scanning Base Servo (20 to 160)..." << std::endl;
    for (int angle = 20; angle <= 160; angle++) {
        setServoAngle(0, angle);
        usleep(20000); // Wait 20ms per degree for smooth movement
    }

    std::cout << "Base returning to center (90)..." << std::endl;
    setServoAngle(0, 90);
    sleep(1);

    // Scan Pitch (Channel 1) Up/Down: 50 to 140
    std::cout << "Scanning Pitch Servo (50 to 140)..." << std::endl;
    for (int angle = 50; angle <= 140; angle++) {
        setServoAngle(1, angle);
        usleep(20000); // Wait 20ms per degree for smooth movement
    }

    std::cout << "Pitch returning to center (90)..." << std::endl;
    setServoAngle(1, 90);
    sleep(1);

    std::cout << "--- Shutdown Complete ---" << std::endl;
    close(i2c_fd);
    return 0;
}
