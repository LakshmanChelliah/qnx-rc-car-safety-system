# Common Module

## Overview

The Common module provides shared headers and definitions used across all components of the QNX RC Car Safety System. It establishes the IPC protocol, hardware configuration, and RTOS timing parameters required for deterministic real-time operation.

This module contains **no compiled code**—only header files that define the system's communication contracts and configuration constants.

## Purpose

- **IPC Protocol Definition**: Standardized message formats for inter-process communication
- **Hardware Abstraction**: Centralized hardware pin mappings and I2C configuration
- **Timing Configuration**: RTOS priorities and timing parameters for real-time guarantees
- **System-Wide Constants**: Shared definitions to ensure consistency across modules

## Directory Structure

```
common/
├── include/
│   ├── constants.h      # Hardware configuration and pin mappings
│   ├── ipc.h            # IPC message protocol definitions
│   └── priorities.h     # QNX thread priorities and timing
└── src/                 # (Empty - header-only module)
```

## Headers

### 1. `constants.h` - Hardware Configuration

Contains hardware-specific constants for I2C communication and motor control.

#### I2C Bus Configuration

```c
#define I2C_DEVICE_PATH "/dev/i2c1"
```

**Usage**: Path to the I2C device file on QNX. Typically `/dev/i2c0` or `/dev/i2c1` depending on hardware.

#### PCA9685 PWM Controller

```c
#define PCA9685_ADDR 0x5F                    // I2C slave address
#define PCA9685_MODE1 0x00                   // Mode register
#define PCA9685_PRESCALE 0xFE                // Prescale register
#define PCA9685_LED0_ON_L 0x06               // LED0 ON register
#define PCA9685_PWM_FREQUENCY 50             // PWM frequency (Hz)
#define PCA9685_PWM_RESOLUTION 4096          // 12-bit resolution
```

**Usage**: Register addresses and configuration for the PCA9685 16-channel PWM driver IC.

#### Motor Pin Mappings

```c
// Motor 1 (Front Right)
#define M1_IN1 15
#define M1_IN2 14

// Motor 2 (Front Left)
#define M2_IN1 12
#define M2_IN2 13

// Motor 3 (Rear Right)
#define M3_IN1 11
#define M3_IN2 10

// Motor 4 (Rear Left)
#define M4_IN1 8
#define M4_IN2 9
```

**Usage**: Maps motor control pins to PCA9685 PWM channels. Each motor requires 2 pins for H-bridge control (forward/reverse).

**Physical Layout**:
```
     [Front]
  M2       M1
[Left]   [Right]
  M4       M3
     [Rear]
```

#### Motor Control Limits

```c
#define MOTOR_SPEED_MIN -1.0f      // Full reverse
#define MOTOR_SPEED_MAX 1.0f       // Full forward
#define MOTOR_SPEED_STOP 0.0f      // Stop
```

**Usage**: Normalized speed range. Negative values = reverse, positive = forward.

---

### 2. `ipc.h` - IPC Protocol Definitions

Defines the message-passing protocol for inter-process communication using QNX IPC.

#### QNX Global Channel Names

```c
#define IPC_DRIVE_CHANNEL "drive_controller_cmd"
#define IPC_HW_LOCK_NAME "pca9685_hw_lock"
```

**Usage**:
- `IPC_DRIVE_CHANNEL`: Global name for connecting to the drive controller server
- `IPC_HW_LOCK_NAME`: Global name for hardware lock to prevent concurrent I2C access

#### Message Type Codes

```c
#define MSG_TYPE_DRIVE_SPEED 0x01
#define MSG_TYPE_DRIVE_CONTROL 0x02
#define MSG_TYPE_EMERGENCY_STOP 0x03
```

**Usage**: Identifies the message type in the `msg_type` field of each message structure.

#### Control Source Enumeration

```c
typedef enum {
    SOURCE_NONE = 0,
    SOURCE_WIFI_JOYSTICK = 1,
    SOURCE_WEBAPP = 2,
    SOURCE_CRUISE_CONTROL = 3
} ControlSource;
```

**Usage**: Identifies which client is currently allowed to send drive commands. Enables multi-source arbitration.

#### IPC Message Structures

##### DriveSpeedCommandMsg

```c
typedef struct __attribute__((packed)) {
    uint16_t msg_type;          // MSG_TYPE_DRIVE_SPEED
    uint16_t source;            // ControlSource
    float left_speed;           // -1.0 to 1.0
    float right_speed;          // -1.0 to 1.0
} DriveSpeedCommandMsg;
```

**Purpose**: Set motor speeds for differential drive.

**Fields**:
- `msg_type`: Must be `MSG_TYPE_DRIVE_SPEED`
- `source`: Must match the currently active control source
- `left_speed`: Speed for left motors (M2, M4)
- `right_speed`: Speed for right motors (M1, M3)

**Size**: 12 bytes (packed)

**Example Usage**:
```c
DriveSpeedCommandMsg msg;
msg.msg_type = MSG_TYPE_DRIVE_SPEED;
msg.source = SOURCE_WIFI_JOYSTICK;
msg.left_speed = 0.5f;   // 50% forward
msg.right_speed = 0.5f;  // 50% forward
MsgSend(server, &msg, sizeof(msg), NULL, 0);
```

##### DriveControlCommandMsg

```c
typedef struct __attribute__((packed)) {
    uint16_t msg_type;          // MSG_TYPE_DRIVE_CONTROL
    uint16_t new_source;        // ControlSource
} DriveControlCommandMsg;
```

**Purpose**: Change the active control source (arbitration switch).

**Fields**:
- `msg_type`: Must be `MSG_TYPE_DRIVE_CONTROL`
- `new_source`: The control source to activate

**Size**: 4 bytes (packed)

**Example Usage**:
```c
DriveControlCommandMsg msg;
msg.msg_type = MSG_TYPE_DRIVE_CONTROL;
msg.new_source = SOURCE_WEBAPP;
MsgSend(server, &msg, sizeof(msg), NULL, 0);
```

##### EmergencyStopCommandMsg

```c
typedef struct __attribute__((packed)) {
    uint16_t msg_type;          // MSG_TYPE_EMERGENCY_STOP
    uint16_t engage;            // 1 = engage, 0 = clear
} EmergencyStopCommandMsg;
```

**Purpose**: Engage or clear emergency stop.

**Fields**:
- `msg_type`: Must be `MSG_TYPE_EMERGENCY_STOP`
- `engage`: `1` = engage e-stop (halt motors), `0` = clear e-stop (resume)

**Size**: 4 bytes (packed)

**Example Usage**:
```c
EmergencyStopCommandMsg msg;
msg.msg_type = MSG_TYPE_EMERGENCY_STOP;
msg.engage = 1;  // Engage e-stop
MsgSend(server, &msg, sizeof(msg), NULL, 0);
```

#### Message Format Guidelines

- All structs use `__attribute__((packed))` to prevent padding
- Message types use `uint16_t` for consistent size across architectures
- Enum values are stored as `uint16_t` for guaranteed binary compatibility
- Floating-point values use IEEE 754 single precision

---

### 3. `priorities.h` - RTOS Configuration

Defines QNX thread priorities and timing parameters for real-time scheduling.

#### QNX Thread Priorities

```c
#define PRIORITY_DRIVE_SERVER 20       // Drive controller main thread
#define PRIORITY_SENSOR_NODE 15        // Sensor reading nodes
#define PRIORITY_JOYSTICK_NODE 15      // Joystick input node
#define PRIORITY_AUTONOMY_NODE 15      // Autonomous control node
```

**Usage**: QNX priority levels (1-63, higher = more urgent). Use with `pthread_setschedparam()` or `on -p <priority>` command.

**Priority Rationale**:
- Drive server runs at highest priority (20) to ensure deterministic motor control
- Input nodes run at lower priority (15) since they can tolerate slight jitter
- Priority inversion avoided by using priority inheritance mutexes

#### Timing Configuration

##### Drive Controller Pulse Rate

```c
#define DRIVE_PULSE_RATE_MS 50         // 50ms = 20Hz update rate
```

**Usage**: Period between hardware actuation pulses. Defines the motor update frequency.

**Trade-offs**:
- **Lower (faster)**: Better responsiveness, higher CPU usage
- **Higher (slower)**: Lower CPU usage, reduced control bandwidth

##### Watchdog Timeout

```c
#define DRIVE_TIMEOUT_PULSES 10        // 10 pulses * 50ms = 500ms timeout
```

**Usage**: Number of pulse cycles without receiving a speed command before motors automatically stop.

**Calculation**: `DRIVE_TIMEOUT_PULSES × DRIVE_PULSE_RATE_MS = 500ms`

**Tuning**:
- Increase for lossy networks or slow clients
- Decrease for faster fault detection

##### IPC Receive Timeout

```c
#define MSG_RECEIVE_TIMEOUT_NS 100000000L  // 100ms
```

**Usage**: Maximum time (in nanoseconds) that `MsgReceive()` blocks waiting for messages.

**Purpose**: Allows the main loop to periodically check for shutdown requests and process timer pulses.

## Usage

### Including Headers

All project modules should include the common headers as needed:

```cpp
#include "../common/include/constants.h"    // Hardware configuration
#include "../common/include/ipc.h"          // IPC protocol
#include "../common/include/priorities.h"   // RTOS parameters
```

Or using absolute paths (if common is in include path):

```cpp
#include <common/include/constants.h>
#include <common/include/ipc.h>
#include <common/include/priorities.h>
```

### Example: IPC Client

```cpp
#include "common/include/ipc.h"
#include <sys/neutrino.h>
#include <stdio.h>

int main() {
    // Connect to drive controller
    int server = name_open(IPC_DRIVE_CHANNEL, 0);
    if (server < 0) {
        perror("name_open");
        return 1;
    }
    
    // Send speed command
    DriveSpeedCommandMsg msg = {
        .msg_type = MSG_TYPE_DRIVE_SPEED,
        .source = SOURCE_WEBAPP,
        .left_speed = 0.75f,
        .right_speed = 0.75f
    };
    
    int result = MsgSend(server, &msg, sizeof(msg), NULL, 0);
    if (result == -1) {
        perror("MsgSend");
    }
    
    name_close(server);
    return 0;
}
```

### Example: Setting Thread Priority

```cpp
#include "common/include/priorities.h"
#include <pthread.h>
#include <sched.h>

void set_realtime_priority() {
    struct sched_param param;
    param.sched_priority = PRIORITY_DRIVE_SERVER;
    
    if (pthread_setschedparam(pthread_self(), SCHED_RR, &param) != 0) {
        perror("pthread_setschedparam");
    }
}
```

## Build Integration

The common module is typically included via compiler flags in dependent modules:

```makefile
# Makefile for drive module
INCLUDES += -I../common/include
```

This makes the headers available to source files without needing relative paths.

## Design Rationale

### Why Header-Only?

- **No Runtime Dependencies**: Modules include headers directly, no linking required
- **Zero Overhead**: Constants resolved at compile-time, no function call overhead
- **Simplified Build**: No need to build common as a separate library
- **Inline Optimization**: Compiler can fully optimize usage of constants

### Why Separate Module?

- **Single Source of Truth**: One place to update hardware configuration
- **Consistent ABI**: All modules use identical message structures
- **Easy Porting**: Hardware-specific values isolated in one location
- **Version Control**: Changes to protocol tracked independently

## Modifying Configuration

### Adding New Message Types

1. **Define Message Type Code** in `ipc.h`:
   ```c
   #define MSG_TYPE_NEW_COMMAND 0x04
   ```

2. **Define Message Structure** in `ipc.h`:
   ```c
   typedef struct __attribute__((packed)) {
       uint16_t msg_type;
       // Add fields here
   } NewCommandMsg;
   ```

3. **Update Server Handler**: Modify drive controller to handle new message type

4. **Rebuild All Modules**: Ensure consistent definitions across system

### Changing Hardware Configuration

1. **Update Constants** in `constants.h`:
   ```c
   #define PCA9685_ADDR 0x40  // New I2C address
   ```

2. **Rebuild All Modules**: Ensures all components use new configuration

3. **Test on Hardware**: Verify hardware responds correctly

### Tuning Real-Time Parameters

1. **Adjust Priorities** in `priorities.h`:
   ```c
   #define PRIORITY_DRIVE_SERVER 25  // Increase priority
   ```

2. **Adjust Timing** in `priorities.h`:
   ```c
   #define DRIVE_PULSE_RATE_MS 25    // Increase to 40Hz
   ```

3. **Rebuild and Profile**: Measure CPU usage and latency

## Testing

Since this is a header-only module, testing is performed indirectly through the modules that use it:

- **IPC Protocol Testing**: Verify message structures in drive controller tests
- **Hardware Configuration Testing**: Validate pin mappings in motor driver tests
- **Integration Testing**: End-to-end system tests verify protocol compliance

## Compatibility

### QNX Version Support

- **QNX Neutrino 7.0+**: Primary target
- **QNX Neutrino 6.6**: Compatible (with minor adjustments)

### Architecture Support

- **ARM 32-bit**: Compatible
- **ARM 64-bit (aarch64le)**: Primary target
- **x86-64**: Compatible (for development/testing)

### C/C++ Standard

- **C11**: All C headers compatible
- **C++11**: All headers compatible with C++ projects
- **C++14/17/20**: Fully compatible

## Related Modules

- **Drive Module** ([README](../drive/README.md)): Motor control server
- **Joystick Node**: Joystick input handler
- **Web App**: Web-based control interface
- **Autonomy Node**: Autonomous navigation

## Versioning

When modifying this module, follow semantic versioning:

- **Major Version**: Breaking changes to IPC protocol or message structures
- **Minor Version**: Backward-compatible additions (new message types)
- **Patch Version**: Documentation updates, code comments

**Current Version**: 1.0.0

## Migration Guide

### Migrating from v0.x to v1.0

- Message structures now use `__attribute__((packed))` for portability
- `ControlSource` enum values changed to start from 0
- `IPC_DRIVE_CHANNEL` renamed from `drive_cmd` to `drive_controller_cmd`

## Best Practices

### For Module Developers

1. **Always Use Constants**: Never hardcode values that exist in common headers
2. **Validate Message Types**: Check `msg_type` field before casting
3. **Handle Unknown Sources**: Gracefully reject messages from unexpected sources
4. **Use Packed Structures**: Ensure binary compatibility across architectures

### For System Integrators

1. **Version Lock**: Ensure all modules compiled with same common headers
2. **Audit Changes**: Review commit history when updating common module
3. **Regression Testing**: Test all modules after updating common headers
4. **Document Deviations**: If custom hardware config required, document clearly

## Troubleshooting

### Mismatched Message Sizes

**Symptom**: Server rejects valid messages
**Cause**: Client and server compiled with different header versions
**Solution**: Rebuild all modules with same common headers

### Wrong Hardware Pins

**Symptom**: Motors connected to wrong channels
**Cause**: Pin mappings in `constants.h` don't match physical wiring
**Solution**: Update pin definitions or rewire hardware

### Priority Inversion

**Symptom**: High-priority drive server blocked by low-priority task
**Cause**: Improper use of mutexes or IPC
**Solution**: Use priority inheritance mutexes, avoid blocking calls in drive server

## Contributing

When modifying common headers:

1. **Document Changes**: Update this README with rationale
2. **Test Thoroughly**: Verify all dependent modules still compile and run
3. **Maintain ABI**: Avoid breaking changes to existing structures
4. **Version Bump**: Update version number and migration guide

## License

[Specify license here]

## Authors

[Specify authors/maintainers here]

## See Also

- [Drive Module README](../drive/README.md)
- [System Architecture Documentation](../../README.md)
- [QNX IPC Documentation](https://www.qnx.com/developers/docs/7.1/index.html#com.qnx.doc.neutrino.sys_arch/topic/ipc.html)
- [QNX Resource Managers](https://www.qnx.com/developers/docs/7.1/index.html#com.qnx.doc.neutrino.prog/topic/resource.html)
