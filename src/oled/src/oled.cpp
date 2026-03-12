/**
 * QNX RC Car Safety System - OLed Module
 * Main Entry Point
 */

#include "../include/OLedDisplay.hpp"

#include <iostream>
#include <csignal>
#include <memory>

volatile sig_atomic_t g_shutdown_requested = 0;
static std::unique_ptr<OledDisplay> g_display = nullptr;

void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        g_shutdown_requested = 1;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "  QNX RC Car Safety System - OLed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    if (signal(SIGINT, signal_handler) == SIG_ERR || signal(SIGTERM, signal_handler) == SIG_ERR) {
        std::cerr << "[Main] ERROR: Failed to register signal handlers." << std::endl;
        return EXIT_FAILURE;
    }
    
    try {
        g_display = std::make_unique<OledDisplay>();
    }
    catch (const std::exception& e) {
        std::cerr << "[Main] FATAL: Failed to create OledDisplay: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    if (g_shutdown_requested) {
        g_display.reset();
        return EXIT_SUCCESS;
    }
    
    // Start the display server (blocks until stop() is called)
    bool success = g_display->start();
    
    if (g_shutdown_requested) {
        std::cout << "\n[Main] Caught signal - initiating graceful shutdown..." << std::endl;
    }
    
    std::cout << "[Main] Display module stopped." << std::endl;
    g_display.reset();
    
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}