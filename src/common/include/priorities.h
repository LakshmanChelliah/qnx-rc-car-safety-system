#ifndef PRIORITIES_H
#define PRIORITIES_H

// ====================
// QNX Thread Priorities
// ====================
// Higher numbers = higher priority in QNX
// Range: 1 (lowest) to 63 (highest) for normal threads

#define PRIORITY_DRIVE_SERVER 10       // Drive controller main thread
#define PRIORITY_SENSOR_NODE 8        // Sensor reading nodes (lower than drive)
#define PRIORITY_JOYSTICK_NODE 9      // Joystick input node
#define PRIORITY_AUTONOMY_NODE 8      // Autonomous control node
#define PRIORITY_OLED_NODE 6           // OLED display node (non-critical, lower than sensor nodes)

// ====================
// Timing Configuration
// ====================

// Drive Controller Pulse Rate
// The hardware update rate (how often motors are actuated)
#define DRIVE_PULSE_RATE_MS 50         // 50ms = 20Hz update rate

// Watchdog Timeout Configuration
// Number of pulses without a command before motors stop
#define DRIVE_TIMEOUT_PULSES 10        // 10 pulses * 50ms = 500ms timeout

// Message Receive Timeout
// Maximum time to block waiting for an IPC message (in nanoseconds)
#define MSG_RECEIVE_TIMEOUT_NS 100000000L  // 100ms

#endif // PRIORITIES_H
