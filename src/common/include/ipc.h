#ifndef IPC_H
#define IPC_H

#include <stdint.h>

// ====================
// QNX IPC Global Names
// ====================
#define IPC_DRIVE_CHANNEL "drive_controller_cmd"
#define IPC_HW_LOCK_NAME "pca9685_hw_lock"

// ====================
// Message Type Codes
// ====================
#define MSG_TYPE_DRIVE_SPEED 0x01
#define MSG_TYPE_DRIVE_CONTROL 0x02
#define MSG_TYPE_EMERGENCY_STOP 0x03

// ====================
// Control Source Enumeration
// ====================
typedef enum {
    SOURCE_NONE = 0,
    SOURCE_JOYSTICK = 1,
    SOURCE_AUTONOMY = 2,
    SOURCE_REMOTE = 3
} ControlSource;

// ====================
// IPC Message Structures
// ====================

// Message: Set Drive Speed
typedef struct __attribute__((packed)) {
    uint16_t msg_type;          // MSG_TYPE_DRIVE_SPEED
    uint16_t source;            // ControlSource (using uint16_t for size guarantee)
    float left_speed;           // -1.0 to 1.0 (negative = reverse)
    float right_speed;          // -1.0 to 1.0 (negative = reverse)
} DriveSpeedCommandMsg;

// Message: Change Active Control Source
typedef struct __attribute__((packed)) {
    uint16_t msg_type;          // MSG_TYPE_DRIVE_CONTROL
    uint16_t new_source;        // ControlSource (using uint16_t for size guarantee)
} DriveControlCommandMsg;

// Message: Emergency Stop
typedef struct __attribute__((packed)) {
    uint16_t msg_type;          // MSG_TYPE_EMERGENCY_STOP
    uint16_t engage;            // 1 = engage e-stop, 0 = clear e-stop
} EmergencyStopCommandMsg;

#endif // IPC_H
