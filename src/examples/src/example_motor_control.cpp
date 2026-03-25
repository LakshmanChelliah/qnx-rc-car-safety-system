#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <devctl.h>
#include <hw/i2c.h>
#include <math.h>
#include <sys/dispatch.h> 
#include <csignal> // Added for signal handling (Ctrl+C)

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
volatile sig_atomic_t keep_running = 1; // Global flag for safe shutdown

// Signal handler to catch Ctrl+C (SIGINT) and termination signals
void sig_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        keep_running = 0; // Tell the main loop to exit gracefully
    }
}

// Custom sleep function that wakes up instantly if Ctrl+C is pressed
void interruptible_sleep(float seconds) {
    int iterations = (int)(seconds * 10.0f); // Check every 100ms
    for (int i = 0; i < iterations && keep_running; i++) {
        usleep(100000); // 100ms
    }
}

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

// Set Motor Speed: speed ranges from -1.0 (full reverse) to 1.0 (full forward)
void setMotorSpeed(int motor_num, float speed) {
    int in1_channel, in2_channel;

    // Map the motor number to the PCA9685 pins
    if (motor_num == 1)      { in1_channel = M1_IN1; in2_channel = M1_IN2; }
    else if (motor_num == 2) { in1_channel = M2_IN1; in2_channel = M2_IN2; }
    else if (motor_num == 3) { in1_channel = M3_IN1; in2_channel = M3_IN2; }
    else if (motor_num == 4) { in1_channel = M4_IN1; in2_channel = M4_IN2; }
    else return; // Invalid motor

    // Clamp speed between -1.0 and 1.0
    if (speed > 1.0f) speed = 1.0f;
    if (speed < -1.0f) speed = -1.0f;

    // 12-bit PWM goes from 0 to 4095
    if (speed == 0.0f) {
        // Stop
        setPWM(in1_channel, 0, 0);
        setPWM(in2_channel, 0, 0);
    } 
    else if (speed > 0.0f) {
        // Forward: SAFETY FIRST
        setPWM(in2_channel, 0, 0); 
        uint16_t pwm_val = (uint16_t)(speed * 4095.0f);
        setPWM(in1_channel, 0, pwm_val);
    } 
    else {
        // Reverse: SAFETY FIRST
        setPWM(in1_channel, 0, 0);
        uint16_t pwm_val = (uint16_t)(-speed * 4095.0f);
        setPWM(in2_channel, 0, pwm_val);
    }
}

// Stop all motors safely
void stopAllMotors() {
    setMotorSpeed(1, 0.0f);
    setMotorSpeed(2, 0.0f);
    setMotorSpeed(3, 0.0f);
    setMotorSpeed(4, 0.0f);
}

int main() {
    std::cout << "--- Adeept Robot QNX Motor Driver ---" << std::endl;

    // Register signal handlers for graceful shutdown on Ctrl+C
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // 0. Implement a QNX Native IPC Lock to prevent multiple instances
    name_attach_t* lock_name = name_attach(NULL, "adeept_motor_lock", 0);
    if (lock_name == NULL) {
        std::cerr << "CRITICAL ERROR: Another motor controller is already running! Aborting to prevent hardware damage." << std::endl;
        return -1;
    }

    // 1. Open the I2C bus device (usually /dev/i2c1 on the Pi GPIO)
    i2c_fd = open("/dev/i2c1", O_RDWR);
    if (i2c_fd < 0) {
        std::cerr << "ERROR: Could not open /dev/i2c1. Is the I2C driver running?" << std::endl;
        name_detach(lock_name, 0);
        return -1;
    }

    // 2. Initialize the chip at 50Hz
    std::cout << "Initializing PCA9685..." << std::endl;
    initPCA9685(50);

    // 3. Adeept Motor Loop (2 iterations)
    for (int i = 0; i < 2 && keep_running; i++) {
        std::cout << "[Loop " << i+1 << "] Forward" << std::endl;
        setMotorSpeed(1, 0.2f); // 0.2f is 20% speed forward
        setMotorSpeed(2, 0.2f);
        setMotorSpeed(3, 0.2f);
        setMotorSpeed(4, 0.2f);
        
        interruptible_sleep(0.5f); // Run for 0.5 seconds (wakes up early if Ctrl+C is pressed)
        if (!keep_running) break;

        std::cout << "Stopping..." << std::endl;
        stopAllMotors();
        
        interruptible_sleep(1.0f); // Stop for 1 second
        if (!keep_running) break;

        std::cout << "[Loop " << i+1 << "] Backward" << std::endl;
        setMotorSpeed(1, -0.2f); // -0.2f is 20% speed backward
        setMotorSpeed(2, -0.2f);
        setMotorSpeed(3, -0.2f);
        setMotorSpeed(4, -0.2f);
        
        interruptible_sleep(0.5f); // Run for 0.5 seconds
        if (!keep_running) break;

        std::cout << "Stopping..." << std::endl;
        stopAllMotors();
        
        interruptible_sleep(1.0f); // Stop for 1 second
    }

    if (!keep_running) {
        std::cout << "\nCaught interrupt signal! Initiating emergency shutdown..." << std::endl;
    }

    std::cout << "--- Shutdown Complete ---" << std::endl;
    // Always stop motors before closing out to prevent runaway!
    stopAllMotors();
    close(i2c_fd);

    // 4. Release the application lock
    name_detach(lock_name, 0);

    return 0;
}