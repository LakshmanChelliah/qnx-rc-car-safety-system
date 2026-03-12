/**
 * QNX RC Car Safety System - Joystick Module
 * Main Entry Point
 */

#include "../include/ButtonMappings.hpp"
#include "../include/MsiGC30V2JoystickDriver.hpp"

#include <iostream>
#include <csignal>
#include <memory>
#include <unistd.h>
#include <sys/neutrino.h>
#include <time.h>
#include <errno.h>

volatile sig_atomic_t g_shutdown_requested = 0;

// Defines for the timer pulse
#define TIMER_PULSE_CODE _PULSE_CODE_MINAVAIL
#define JOYSTICK_TICK_RATE_MS 50 // 20Hz update rate
#define PRIORITY_JOYSTICK_TICK 21 // Standard QNX high-priority baseline

void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        g_shutdown_requested = 1;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "  QNX RC Car Safety System - Joystick" << std::endl;
    std::cout << "========================================" << std::endl;

    if (signal(SIGINT, signal_handler) == SIG_ERR || signal(SIGTERM, signal_handler) == SIG_ERR) {
        std::cerr << "[Main] ERROR: Failed to register signal handlers." << std::endl;
        return EXIT_FAILURE;
    }

    // 1. Create the mappings and connect to the Drive IPC
    auto mappings = std::make_shared<ButtonMappings>();
    if (!mappings->connectIPC()) {
        std::cerr << "[Main] FATAL: Could not establish IPC." << std::endl;
        return EXIT_FAILURE;
    }

    // 2. Initialize and start the HID Driver
    MsiGC30V2JoystickDriver driver(mappings);
    if (!driver.start()) {
        std::cerr << "[Main] FATAL: Failed to start joystick driver." << std::endl;
        mappings->disconnectIPC();
        return EXIT_FAILURE;
    }

    // 3. Set up local Channel and Timer for the fixed-rate control loop
    int chid = ChannelCreate(0);
    if (chid == -1) {
        std::cerr << "[Main] FATAL: Failed to create channel." << std::endl;
        return EXIT_FAILURE;
    }

    int coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    
    struct sigevent timer_event;
    SIGEV_PULSE_INIT(&timer_event, coid, PRIORITY_JOYSTICK_TICK, TIMER_PULSE_CODE, 0);
    
    timer_t timer_id;
    if (timer_create(CLOCK_MONOTONIC, &timer_event, &timer_id) == -1) {
        std::cerr << "[Main] FATAL: Failed to create monotonic timer." << std::endl;
        return EXIT_FAILURE;
    }

    struct itimerspec timer_spec;
    timer_spec.it_value.tv_sec = 0;
    timer_spec.it_value.tv_nsec = JOYSTICK_TICK_RATE_MS * 1000000L;
    timer_spec.it_interval.tv_sec = 0;
    timer_spec.it_interval.tv_nsec = JOYSTICK_TICK_RATE_MS * 1000000L;

    timer_settime(timer_id, 0, &timer_spec, NULL);

    std::cout << "[Main] Hardware driver online. Running control loop at " 
              << (1000 / JOYSTICK_TICK_RATE_MS) << "Hz. Press Ctrl+C to shut down." << std::endl;

    // 4. Main Event Loop
    struct _pulse pulse;
    while (!g_shutdown_requested) {
        // MsgReceive blocks perfectly until the timer pulse arrives
        int rcvid = MsgReceive(chid, &pulse, sizeof(pulse), NULL);

        if (rcvid == -1) {
            // MsgReceive returns -1 and sets errno to EINTR if interrupted by a signal (Ctrl+C)
            if (errno == EINTR) {
                break; 
            }
            std::cerr << "[Main] MsgReceive error: " << strerror(errno) << std::endl;
            break;
        }

        if (rcvid == 0 && pulse.code == TIMER_PULSE_CODE) {
            mappings->publishTick();
        }
    }

    std::cout << "\n[Main] Shutdown signal caught. Cleaning up..." << std::endl;
    
    // Safety check: tell the motors to stop before we disconnect
    ControllerState stopState; 
    stopState.lx = 128; // Center X
    stopState.ly = 128; // Center Y
    mappings->updateState(stopState); 
    mappings->publishTick(); // Force one last zero-speed publish

    // Cleanup OS resources
    timer_delete(timer_id);
    ConnectDetach(coid);
    ChannelDestroy(chid);

    driver.stop();
    mappings->disconnectIPC();

    std::cout << "========================================" << std::endl;
    std::cout << "  Shutdown Complete" << std::endl;
    std::cout << "========================================" << std::endl;

    return EXIT_SUCCESS;
}