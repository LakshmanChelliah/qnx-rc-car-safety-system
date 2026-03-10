/**
 * QNX RC Car Safety System - Drive Module
 * Main Entry Point
 * 
 * Responsibilities:
 * - Process lifecycle management
 * - Signal handling for graceful shutdown
 * - DriveController instantiation and startup
 */

#include "../include/DriveController.hpp"

#include <iostream>
#include <csignal>
#include <memory>

int run_imu_test();

/**
 * Global shutdown flag for signal handler.
 * Must be volatile sig_atomic_t for async-signal-safety.
 */
volatile sig_atomic_t g_shutdown_requested = 0;

static std::unique_ptr<DriveController> g_controller = nullptr;

/**
 * Signal handler for SIGINT (Ctrl+C) and SIGTERM.
 * Only sets a flag to ensure async-signal-safety.
 */
void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        g_shutdown_requested = 1;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "  QNX RC Car Safety System - Drive" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Register signal handlers for graceful shutdown
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        std::cerr << "[Main] ERROR: Failed to register SIGINT handler." << std::endl;
        return EXIT_FAILURE;
    }
    
    if (signal(SIGTERM, signal_handler) == SIG_ERR) {
        std::cerr << "[Main] ERROR: Failed to register SIGTERM handler." << std::endl;
        return EXIT_FAILURE;
    }
    
    std::cout << "[Main] Signal handlers registered (Ctrl+C for graceful shutdown)." << std::endl;
    
    // Create the drive controller
    try {
        g_controller = std::make_unique<DriveController>();
    }
    catch (const std::exception& e) {
        std::cerr << "[Main] FATAL: Failed to create DriveController: " 
                  << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    // Start the controller (blocks until stop() is called)
    if (g_shutdown_requested && g_controller != nullptr) {
        std::cout << "\n[Main] Shutdown requested before start - exiting cleanly." << std::endl;
        g_controller.reset();
        return EXIT_SUCCESS;
    }
    
    bool success = (run_imu_test() == 0);
    
    if (g_shutdown_requested) {
        std::cout << "\n[Main] Caught signal - initiating graceful shutdown..." << std::endl;
    }
    
    // Clean shutdown
    std::cout << "[Main] Drive controller stopped." << std::endl;
    g_controller.reset();
    
    std::cout << "========================================" << std::endl;
    std::cout << "  Shutdown Complete" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
