#ifndef ULTRASONIC_H
#define ULTRASONIC_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint32_t last_distance_cm;
    uint64_t last_timestamp_us;
    uint8_t valid;
} UltrasonicSharedState;

#define ULTRASONIC_SHM_NAME "/ultrasonic_shm"

#define ULTRASONIC_GPIO_TRIG 23
#define ULTRASONIC_GPIO_ECHO 24

#define ULTRASONIC_MIN_CM 2U
#define ULTRASONIC_MAX_CM 400U

#endif // ULTRASONIC_H

