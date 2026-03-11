#include "../include/DriveController.hpp"
#include "../../common/include/constants.h"
#include "../../common/include/priorities.h"

#include <iostream>
#include <cstring>
#include <time.h>
#include <sys/neutrino.h>
#include <sys/iomsg.h>
#include <errno.h>

extern volatile sig_atomic_t g_shutdown_requested;

#define TIMER_PULSE_CODE (_PULSE_CODE_MINAVAIL + 1)

DriveController::DriveController() 
    : motor_driver_(nullptr),
      channel_(nullptr),
      timer_id_(-1),
      timer_(-1),
      m_oledCoid(-1),
      current_active_source_(SOURCE_NONE),
      is_e_stopped_(false),
      timeout_counter_(0),
      target_speed_left_(0.0f),
      target_speed_right_(0.0f),
      running_(false) {
    
    std::cout << "[DriveController] Creating motor driver..." << std::endl;
    
    try {
        motor_driver_ = std::make_unique<FourMotorI2CDriver>();
    }
    catch (const std::exception& e) {
        std::cerr << "[DriveController] ERROR: Failed to create motor driver: " 
                  << e.what() << std::endl;
        throw;
    }
}

DriveController::~DriveController() {
    std::cout << "[DriveController] Destructor: Cleaning up IPC resources..." << std::endl;
    
    if (timer_ != -1) {
        timer_delete(timer_);
        timer_ = -1;
    }
    
    if (timer_id_ != -1) {
        ConnectDetach(timer_id_);
        timer_id_ = -1;
    }
    
    if (channel_ != nullptr) {
        name_detach(channel_, 0);
        channel_ = nullptr;
    }
    
    std::cout << "[DriveController] IPC cleanup complete." << std::endl;
}

bool DriveController::start() {
    std::cout << "[DriveController] Starting drive controller server..." << std::endl;
    
    if (!motor_driver_->init()) {
        std::cerr << "[DriveController] ERROR: Hardware initialization failed." << std::endl;
        return false;
    }
    
    channel_ = name_attach(NULL, IPC_DRIVE_CHANNEL, 0);
    if (channel_ == nullptr) {
        std::cerr << "[DriveController] ERROR: Failed to attach IPC channel '"
                  << IPC_DRIVE_CHANNEL << "'. Error: " << strerror(errno) << std::endl;
        return false;
    }
    std::cout << "[DriveController] IPC channel '" << IPC_DRIVE_CHANNEL << "' attached." << std::endl;
    
    timer_id_ = ConnectAttach(0, 0, channel_->chid, _NTO_SIDE_CHANNEL, 0);
    if (timer_id_ == -1) {
        std::cerr << "[DriveController] ERROR: Failed to create timer connection." << std::endl;
        name_detach(channel_, 0);
        channel_ = nullptr;
        return false;
    }

    connectIPC();
    
    SIGEV_PULSE_INIT(&timer_event_, timer_id_, PRIORITY_DRIVE_SERVER, TIMER_PULSE_CODE, 0);
    
    if (timer_create(CLOCK_MONOTONIC, &timer_event_, &timer_) == -1) {
        std::cerr << "[DriveController] ERROR: Failed to create timer." << std::endl;
        ConnectDetach(timer_id_);
        timer_id_ = -1;
        name_detach(channel_, 0);
        channel_ = nullptr;
        return false;
    }
    
    struct itimerspec timer_spec;
    timer_spec.it_value.tv_sec = 0;
    timer_spec.it_value.tv_nsec = DRIVE_PULSE_RATE_MS * 1000000L;
    timer_spec.it_interval.tv_sec = 0;
    timer_spec.it_interval.tv_nsec = DRIVE_PULSE_RATE_MS * 1000000L;
    
    if (timer_settime(timer_, 0, &timer_spec, NULL) == -1) {
        std::cerr << "[DriveController] ERROR: Failed to start timer." << std::endl;
        timer_delete(timer_);
        timer_ = -1;
        ConnectDetach(timer_id_);
        timer_id_ = -1;
        name_detach(channel_, 0);
        channel_ = nullptr;
        return false;
    }
    
    std::cout << "[DriveController] Timer started at " << DRIVE_PULSE_RATE_MS 
              << "ms intervals (" << (1000 / DRIVE_PULSE_RATE_MS) << "Hz)" << std::endl;
    
    struct sched_param param;
    param.sched_priority = PRIORITY_DRIVE_SERVER;
    if (sched_setparam(0, &param) == -1) {
        std::cerr << "[DriveController] WARNING: Failed to set thread priority." << std::endl;
    }
    
    std::cout << "[DriveController] Server ready. Entering main loop..." << std::endl;
    std::cout << "  - Active Source: NONE" << std::endl;
    std::cout << "  - Watchdog Timeout: " << (DRIVE_TIMEOUT_PULSES * DRIVE_PULSE_RATE_MS) << "ms" << std::endl;
    
    running_ = true;
    
    union {
        struct _pulse pulse;
        DriveSpeedCommandMsg speed_msg;
        DriveControlCommandMsg control_msg;
        EmergencyStopCommandMsg estop_msg;
        char buf[512];
    } msg;
    
    struct _msg_info info;
    
    while (running_) {
        if (g_shutdown_requested) {
            std::cout << "[DriveController] External shutdown requested, exiting loop..." << std::endl;
            break;
        }
        
        int rcvid = MsgReceive(channel_->chid, &msg, sizeof(msg), &info);
        
        if (rcvid == -1) {
            if (errno == EINTR) {
                if (!running_) break;
                continue;
            }
            std::cerr << "[DriveController] ERROR: MsgReceive failed: " 
                      << strerror(errno) << std::endl;
            break;
        }
        
        if (rcvid == 0) {
            if (msg.pulse.code == TIMER_PULSE_CODE) {
                processHardwarePulse();
            }
        }
        else {
            // Validate message size before processing
            if (info.msglen < static_cast<_Ssize64t>(sizeof(uint16_t))) {
                std::cerr << "[DriveController] WARNING: Message too small (" 
                          << info.msglen << " bytes), ignoring." << std::endl;
                MsgError(rcvid, EINVAL);
                continue;
            }
            
            uint16_t msg_type = *((uint16_t*)&msg.buf[0]);
            
            switch (msg_type) {
                case MSG_TYPE_DRIVE_SPEED:
                    if (info.msglen != static_cast<_Ssize64t>(sizeof(DriveSpeedCommandMsg))) {
                        std::cerr << "[DriveController] WARNING: Invalid DRIVE_SPEED size (" 
                                  << info.msglen << " vs " << sizeof(DriveSpeedCommandMsg) 
                                  << "), ignoring." << std::endl;
                        MsgError(rcvid, EINVAL);
                        break;
                    }
                    handleSpeedCommand(msg.speed_msg);
                    MsgReply(rcvid, EOK, NULL, 0);
                    break;
                    
                case MSG_TYPE_DRIVE_CONTROL:
                    if (info.msglen != static_cast<_Ssize64t>(sizeof(DriveControlCommandMsg))) {
                        std::cerr << "[DriveController] WARNING: Invalid DRIVE_CONTROL size (" 
                                  << info.msglen << " vs " << sizeof(DriveControlCommandMsg) 
                                  << "), ignoring." << std::endl;
                        MsgError(rcvid, EINVAL);
                        break;
                    }
                    handleControlCommand(msg.control_msg);
                    MsgReply(rcvid, EOK, NULL, 0);
                    break;
                    
                case MSG_TYPE_EMERGENCY_STOP:
                    if (info.msglen != static_cast<_Ssize64t>(sizeof(EmergencyStopCommandMsg))) {
                        std::cerr << "[DriveController] WARNING: Invalid ESTOP size (" 
                                  << info.msglen << " vs " << sizeof(EmergencyStopCommandMsg) 
                                  << "), ignoring." << std::endl;
                        MsgError(rcvid, EINVAL);
                        break;
                    }
                    handleEStop(msg.estop_msg);
                    MsgReply(rcvid, EOK, NULL, 0);
                    break;
                    
                case _IO_CONNECT:
                    // QNX kernel handshake for name_open() - accept connection
                    MsgReply(rcvid, EOK, NULL, 0);
                    break;
                    
                default:
                    std::cerr << "[DriveController] WARNING: Unknown message type: " 
                              << (int)msg_type << std::endl;
                    MsgError(rcvid, EINVAL);
                    break;
            }
        }
    }
    
    std::cout << "[DriveController] Main loop exited cleanly." << std::endl;
    return true;
}

void DriveController::stop() {
    std::cout << "[DriveController] Stop requested..." << std::endl;
    running_ = false;
}

bool DriveController::connectIPC() {
    std::cout << "[DriveController] Waiting for OLED IPC ('" << IPC_OLED_CHANNEL << "')..." << std::endl;

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Retry for up to 10 seconds so drive startup is not blocked if OLED is unavailable
    while (m_oledCoid == -1 && !g_shutdown_requested) {
        m_oledCoid = name_open(IPC_OLED_CHANNEL, 0);
        if (m_oledCoid == -1) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
            if (elapsed >= 10.0) {
                std::cerr << "[DriveController] OLED IPC not found after 10s, continuing without display." << std::endl;
                return false;
            }
            usleep(500000); // Wait half a second before retrying
        }
    }

    if (g_shutdown_requested) return false;

    std::cout << "[DriveController] Connected to OLED Controller IPC." << std::endl;
    return true;
}

void DriveController::disconnectIPC() {
    if (m_oledCoid != -1) {
        name_close(m_oledCoid);
        m_oledCoid = -1;
    }
}

// ====================
// Message Handlers
// ====================

void DriveController::handleSpeedCommand(const DriveSpeedCommandMsg& msg) {
    // Check if message is from the currently active source
    if ((ControlSource)msg.source != current_active_source_) {
        // Ignore commands from non-active sources
        return;
    }
    
    // Check if e-stopped
    if (is_e_stopped_) {
        // Ignore all speed commands during e-stop
        return;
    }
    
    // Valid command received - store target speeds and reset watchdog
    target_speed_left_ = msg.left_speed;
    target_speed_right_ = msg.right_speed;
    resetTimeout();
    
    // Debug output (can be removed for production)
    // std::cout << "[DriveController] Speed command: L=" << msg.left_speed 
    //           << " R=" << msg.right_speed << std::endl;
}

void DriveController::handleControlCommand(const DriveControlCommandMsg& msg) {
    std::cout << "[DriveController] Control source changed: " 
              << (int)current_active_source_ << " -> " << (int)msg.new_source << std::endl;
    
    current_active_source_ = (ControlSource)msg.new_source;
    
    // Reset speeds to prevent jerking when switching control sources
    target_speed_left_ = 0.0f;
    target_speed_right_ = 0.0f;
    resetTimeout();
    
    // Immediately apply stop
    motor_driver_->stopAll();
}

void DriveController::handleEStop(const EmergencyStopCommandMsg& msg) {
    if (msg.engage) {
        std::cout << "[DriveController] *** EMERGENCY STOP ENGAGED ***" << std::endl;
        is_e_stopped_ = true;
        target_speed_left_ = 0.0f;
        target_speed_right_ = 0.0f;
        motor_driver_->stopAll();
    }
    else {
        std::cout << "[DriveController] Emergency stop CLEARED." << std::endl;
        is_e_stopped_ = false;
        resetTimeout();
    }

    if (m_oledCoid != -1) {
        // Send the exact same message struct over to the display.
        // MsgSend blocks until the display server calls MsgReply (which we set up to be instant).
        int result = MsgSend(m_oledCoid, &msg, sizeof(msg), NULL, 0);
        
        if (result == -1) {
            std::cerr << "[DriveController] WARNING: Failed to send E-Stop to Display: " 
                      << strerror(errno) << std::endl;
        }
    } else {
        // This is normal if the display module isn't currently running
        std::cerr << "[DriveController] WARNING: OLED module not found (" 
                  << IPC_OLED_CHANNEL << "). E-Stop state not displayed." << std::endl;
    }
}

// ====================
// Hardware Pulse Handler
// ====================

void DriveController::processHardwarePulse() {
    // Increment timeout counter (capped to prevent overflow)
    if (timeout_counter_ < DRIVE_TIMEOUT_PULSES + 1) {
        timeout_counter_++;
    }
    
    // Check for timeout
    if (timeout_counter_ >= DRIVE_TIMEOUT_PULSES) {
        // Watchdog timeout
        if (target_speed_left_ != 0.0f || target_speed_right_ != 0.0f) {
            std::cout << "[DriveController] Watchdog timeout! Stopping motors." << std::endl;
            target_speed_left_ = 0.0f;
            target_speed_right_ = 0.0f;
        } else if (timeout_counter_ == DRIVE_TIMEOUT_PULSES) {
            std::cout << "[DriveController] Watchdog timeout (motors already stopped)." << std::endl;
        }
    }
    
    // If e-stopped, force speeds to zero
    if (is_e_stopped_) {
        motor_driver_->stopAll();
        return;
    }
    
    // Actuate hardware with current target speeds
    motor_driver_->setSpeeds(target_speed_left_, target_speed_right_);
}

void DriveController::resetTimeout() {
    timeout_counter_ = 0;
}
