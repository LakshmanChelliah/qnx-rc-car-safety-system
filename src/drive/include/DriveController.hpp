#ifndef DRIVECONTROLLER_HPP
#define DRIVECONTROLLER_HPP

#include "FourMotorI2CDriver.hpp"
#include "../../common/include/ipc.h"

#include <sys/dispatch.h>
#include <memory>
#include <csignal>

/**
 * DriveController - RTOS Logic Layer
 * 
 * Manages the QNX message-passing state machine for drive control.
 * Features:
 * - Multi-source input arbitration (Joystick, Autonomy, Remote)
 * - Emergency stop override
 * - Watchdog timeout (stops motors if no commands received)
 * - Periodic hardware actuation via QNX timer pulses
 */
class DriveController {
public:
    /**
     * Constructor
     * Creates the hardware driver instance.
     */
    DriveController();
    
    /**
     * Destructor
     * Cleans up IPC resources. Motor stop is handled by FourMotorI2CDriver destructor.
     */
    ~DriveController();
    
    /**
     * Start the drive controller server
     * Attaches IPC channel, initializes hardware, creates timer pulse, and enters main loop.
     * This function blocks until stop() is called or an error occurs.
     * 
     * @return true on clean exit, false on error
     */
    bool start();
    
    /**
     * Stop the drive controller (call from signal handler)
     * Sets the internal flag to exit the main loop cleanly.
     */
    void stop();

private:
    // Hardware driver (owned)
    std::unique_ptr<FourMotorI2CDriver> motor_driver_;
    
    // QNX IPC Resources
    name_attach_t* channel_;
    int timer_id_;
    struct sigevent timer_event_;
    timer_t timer_;
    
    // State Machine Variables
    ControlSource current_active_source_;
    bool is_e_stopped_;
    int timeout_counter_;
    float target_speed_left_;
    float target_speed_right_;
    volatile sig_atomic_t running_;
    
    // Message Handlers
    void handleSpeedCommand(const DriveSpeedCommandMsg& msg);
    void handleControlCommand(const DriveControlCommandMsg& msg);
    void handleEStop(const EmergencyStopCommandMsg& msg);
    
    // Hardware Pulse Handler (called periodically by timer)
    void processHardwarePulse();
    
    // Helper to reset timeout watchdog
    void resetTimeout();
};

#endif // DRIVECONTROLLER_HPP
