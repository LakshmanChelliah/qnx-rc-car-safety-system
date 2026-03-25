#include "../include/ButtonMappings.hpp"
#include "../../common/include/ipc.h"

#include <iostream>
#include <cstring>
#include <errno.h>
#include <algorithm>
#include <cmath>
#include <unistd.h>
#include <sys/dispatch.h>
#include <csignal>

extern volatile sig_atomic_t g_shutdown_requested;

ButtonMappings::ButtonMappings() : m_driveCoid(-1) {}

ButtonMappings::~ButtonMappings() {
    disconnectIPC();
}

bool ButtonMappings::connectIPC() {
    std::cout << "[Joystick] Waiting for Drive Controller IPC ('" << IPC_DRIVE_CHANNEL << "')..." << std::endl;
    
    // Retry loop so joystick gracefully waits for drive to launch
    while (m_driveCoid == -1 && !g_shutdown_requested) {
        m_driveCoid = name_open(IPC_DRIVE_CHANNEL, 0);
        if (m_driveCoid == -1) {
            usleep(500000); // Wait half a second before retrying
        }
    }

    if (g_shutdown_requested) return false;

    std::cout << "[Joystick] Connected to Drive Controller IPC." << std::endl;
    return true;
}

void ButtonMappings::disconnectIPC() {
    if (m_driveCoid != -1) {
        name_close(m_driveCoid);
        m_driveCoid = -1;
    }
}

// Quickly lock, update, and unlock. Called by HID thread.
void ButtonMappings::updateState(const ControllerState& newState) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_latestState = newState;
}

// Called by main thread timer loop
void ButtonMappings::publishTick() {
    if (m_driveCoid == -1) return;

    ControllerState currentState;
    {
        // Safely grab a snapshot of the latest state
        std::lock_guard<std::mutex> lock(m_stateMutex);
        currentState = m_latestState;
    }

    // Detect Button Presses (Edge Detection)
    uint16_t changedButtons = currentState.buttons ^ m_prevState.buttons;
    uint16_t pressedButtons = changedButtons & currentState.buttons;

    // --- Critical State Controls ---
    if (pressedButtons & BTN_START) {
        std::cout << "[Joystick] START pressed: Clearing E-Stop & taking control." << std::endl;
        sendEmergencyStopCommand(0); 
        sendDriveControlCommand(SOURCE_USB_JOYSTICK);
    }

    if (pressedButtons & BTN_BACK) {
        std::cout << "[Joystick] BACK pressed: Engaging E-Stop!" << std::endl;
        sendEmergencyStopCommand(1); 
    }

    // --- Placeholders for future features ---
    if (pressedButtons & BTN_A) { std::cout << "[Joystick] A pressed (Placeholder)" << std::endl; }
    if (pressedButtons & BTN_B) { std::cout << "[Joystick] B pressed (Placeholder)" << std::endl; }
    if (pressedButtons & BTN_X) { std::cout << "[Joystick] X pressed (Placeholder)" << std::endl; }
    if (pressedButtons & BTN_Y) { std::cout << "[Joystick] Y pressed (Placeholder)" << std::endl; }
    if (pressedButtons & BTN_LB) { std::cout << "[Joystick] LB pressed (Placeholder)" << std::endl; }
    if (pressedButtons & BTN_RB) { std::cout << "[Joystick] RB pressed (Placeholder)" << std::endl; }
    if (pressedButtons & BTN_LT) { std::cout << "[Joystick] LT pressed (Placeholder)" << std::endl; }
    if (pressedButtons & BTN_RT) { std::cout << "[Joystick] RT pressed (Placeholder)" << std::endl; }
    if (pressedButtons & BTN_LS) { std::cout << "[Joystick] L-Stick clicked (Placeholder)" << std::endl; }
    if (pressedButtons & BTN_RS) { std::cout << "[Joystick] R-Stick clicked (Placeholder)" << std::endl; }

    // Analog Steering Logic & Watchdog Feeding
    float throttle = -(currentState.ly - 128.0f) / 128.0f; 
    float steering =  (currentState.lx - 128.0f) / 128.0f; 

    if (std::abs(throttle) < 0.05f) throttle = 0.0f;
    if (std::abs(steering) < 0.05f) steering = 0.0f;

    float leftSpeed  = throttle + steering;
    float rightSpeed = throttle - steering;

    leftSpeed  = std::max(-1.0f, std::min(1.0f, leftSpeed));
    rightSpeed = std::max(-1.0f, std::min(1.0f, rightSpeed));

    sendDriveSpeedCommand(leftSpeed, rightSpeed);

    m_prevState = currentState;
}

void ButtonMappings::sendDriveControlCommand(uint16_t source) {
    DriveControlCommandMsg msg;
    msg.msg_type = MSG_TYPE_DRIVE_CONTROL;
    msg.new_source = source;
    MsgSend(m_driveCoid, &msg, sizeof(msg), NULL, 0);
}

void ButtonMappings::sendEmergencyStopCommand(uint16_t engage) {
    EmergencyStopCommandMsg msg;
    msg.msg_type = MSG_TYPE_EMERGENCY_STOP;
    msg.engage = engage;
    MsgSend(m_driveCoid, &msg, sizeof(msg), NULL, 0);
}

void ButtonMappings::sendDriveSpeedCommand(float leftSpeed, float rightSpeed) {
    DriveSpeedCommandMsg msg;
    msg.msg_type = MSG_TYPE_DRIVE_SPEED;
    msg.source = SOURCE_USB_JOYSTICK;
    msg.left_speed = leftSpeed;
    msg.right_speed = rightSpeed;
    MsgSend(m_driveCoid, &msg, sizeof(msg), NULL, 0);
}