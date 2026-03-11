#ifndef BUTTON_MAPPINGS_HPP
#define BUTTON_MAPPINGS_HPP

#include <stdint.h>
#include <string>
#include <mutex>

// Button Bitmasks (Mapped for MSI GC30 V2 in Analog Mode)
#define BTN_X       (1 << 0)
#define BTN_A       (1 << 1)
#define BTN_B       (1 << 2)
#define BTN_Y       (1 << 3)
#define BTN_LB      (1 << 4)
#define BTN_RB      (1 << 5)
#define BTN_LT      (1 << 6)
#define BTN_RT      (1 << 7)
#define BTN_BACK    (1 << 8)
#define BTN_START   (1 << 9)
#define BTN_LS      (1 << 10)
#define BTN_RS      (1 << 11)

struct ControllerState {
    uint16_t buttons = 0;
    uint8_t dpad = 0x0F;
    uint8_t lx = 128;
    uint8_t ly = 128;
    uint8_t rx = 128;
    uint8_t ry = 128;
};

class ButtonMappings {
public:
    ButtonMappings();
    ~ButtonMappings();

    bool connectIPC();
    void disconnectIPC();
    
    // Called asynchronously by the HID Driver to simply update the latest data
    void updateState(const ControllerState& newState);

    // Called on a strict timer from the main loop to process and publish commands
    void publishTick();

private:
    int m_driveCoid;
    
    // Thread safety variables
    std::mutex m_stateMutex;
    ControllerState m_latestState;
    ControllerState m_prevState;

    // Helper functions for IPC
    void sendDriveControlCommand(uint16_t source);
    void sendEmergencyStopCommand(uint16_t engage);
    void sendDriveSpeedCommand(float leftSpeed, float rightSpeed);
};

#endif // BUTTON_MAPPINGS_HPP