#include "../include/UltrasonicNode.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <hw/inout.h>
#include <iostream>
#include <sys/dispatch.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace {
constexpr uintptr_t BCM2711_GPIO_BASE = 0xFE200000;
constexpr size_t GPIO_REG_SPACE = 0x1000;

constexpr uintptr_t GPFSEL0 = 0x00;
constexpr uintptr_t GPSET0 = 0x1C;
constexpr uintptr_t GPCLR0 = 0x28;
constexpr uintptr_t GPLEV0 = 0x34;

void configureGpioMode(uintptr_t gpio_base, int pin, bool output) {
    uintptr_t reg = GPFSEL0 + static_cast<uintptr_t>((pin / 10) * 4);
    unsigned shift = static_cast<unsigned>((pin % 10) * 3);
    uint32_t value = in32(gpio_base + reg);
    value &= ~(0x7u << shift);
    if (output) {
        value |= (0x1u << shift);
    }
    out32(gpio_base + reg, value);
}

size_t alignedMutexOffset() {
    const size_t a = alignof(pthread_mutex_t);
    const size_t n = sizeof(UltrasonicSharedState);
    return (n + (a - 1)) & ~(a - 1);
}

size_t sharedMapSize() {
    return alignedMutexOffset() + sizeof(pthread_mutex_t);
}
} // namespace

UltrasonicNode::UltrasonicNode()
    : shm_fd_(-1),
      shared_state_(nullptr),
      shared_mutex_(nullptr),
      gpio_base_(MAP_DEVICE_FAILED),
      drive_coid_(-1),
      estop_engaged_(false),
      running_(false) {}

UltrasonicNode::~UltrasonicNode() {
    stop();
    cleanupSharedState();
    if (drive_coid_ != -1) {
        name_close(drive_coid_);
        drive_coid_ = -1;
    }
    if (gpio_base_ != MAP_DEVICE_FAILED) {
        munmap_device_io(gpio_base_, GPIO_REG_SPACE);
        gpio_base_ = MAP_DEVICE_FAILED;
    }
}

bool UltrasonicNode::start() {
    if (!initSharedState()) {
        std::cerr << "[UltrasonicNode] Failed to initialize shared memory." << std::endl;
        return false;
    }

    if (!initGpio()) {
        std::cerr << "[UltrasonicNode] Failed to configure GPIO pins." << std::endl;
        return false;
    }

    running_ = true;
    std::cout << "[UltrasonicNode] Started (Trig=GPIO" << ULTRASONIC_GPIO_TRIG
              << ", Echo=GPIO" << ULTRASONIC_GPIO_ECHO << ")." << std::endl;
    int log_divider = 0;

    while (running_) {
        uint32_t distance_cm = 0;
        bool valid = measureDistanceCm(distance_cm);
        uint64_t ts_us = monotonicUs();

        if (pthread_mutex_lock(shared_mutex_) == 0) {
            shared_state_->last_timestamp_us = ts_us;
            shared_state_->valid = valid ? 1 : 0;
            if (valid) {
                shared_state_->last_distance_cm = distance_cm;
            }
            pthread_mutex_unlock(shared_mutex_);
        }

        updateEmergencyStop(valid, distance_cm);

        // Print one human-readable distance line every ~1s (loop is 10Hz).
        log_divider++;
        if (log_divider >= 10) {
            log_divider = 0;
            if (valid) {
                std::cout << "Distance : " << distance_cm << " cm" << std::endl;
            } else {
                std::cout << "Distance : --- cm" << std::endl;
            }
        }

        usleep(100000); // 10 Hz update
    }

    return true;
}

void UltrasonicNode::stop() {
    running_ = false;
}

bool UltrasonicNode::initSharedState() {
    shm_fd_ = shm_open(ULTRASONIC_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd_ == -1) {
        std::cerr << "[UltrasonicNode] shm_open failed: " << strerror(errno) << std::endl;
        return false;
    }

    const size_t map_size = sharedMapSize();
    if (ftruncate(shm_fd_, static_cast<off_t>(map_size)) == -1) {
        std::cerr << "[UltrasonicNode] ftruncate failed: " << strerror(errno) << std::endl;
        return false;
    }

    void* addr = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (addr == MAP_FAILED) {
        std::cerr << "[UltrasonicNode] mmap failed: " << strerror(errno) << std::endl;
        return false;
    }

    shared_state_ = reinterpret_cast<UltrasonicSharedState*>(addr);
    shared_mutex_ = reinterpret_cast<pthread_mutex_t*>(
        reinterpret_cast<char*>(addr) + alignedMutexOffset());

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(shared_mutex_, &attr);
    pthread_mutexattr_destroy(&attr);

    pthread_mutex_lock(shared_mutex_);
    shared_state_->last_distance_cm = 0;
    shared_state_->last_timestamp_us = 0;
    shared_state_->valid = 0;
    pthread_mutex_unlock(shared_mutex_);

    return true;
}

void UltrasonicNode::cleanupSharedState() {
    if (shared_state_ != nullptr) {
        const size_t map_size = sharedMapSize();
        munmap(shared_state_, map_size);
        shared_state_ = nullptr;
        shared_mutex_ = nullptr;
    }

    if (shm_fd_ != -1) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
}

bool UltrasonicNode::connectDriveIpc() {
    if (drive_coid_ != -1) {
        return true;
    }
    drive_coid_ = name_open(IPC_DRIVE_CHANNEL, 0);
    return drive_coid_ != -1;
}

void UltrasonicNode::updateEmergencyStop(bool distance_valid, uint32_t distance_cm) {
    const uint32_t estop_threshold_cm = 25;
    bool should_engage = distance_valid && (distance_cm < estop_threshold_cm);

    if (should_engage == estop_engaged_) {
        return;
    }

    if (!connectDriveIpc()) {
        return;
    }

    EmergencyStopCommandMsg msg{};
    msg.msg_type = MSG_TYPE_EMERGENCY_STOP;
    msg.engage = should_engage ? 1 : 0;

    int result = MsgSend(drive_coid_, &msg, sizeof(msg), nullptr, 0);
    if (result == -1) {
        name_close(drive_coid_);
        drive_coid_ = -1;
        return;
    }

    estop_engaged_ = should_engage;
    std::cout << "[UltrasonicNode] E-STOP " << (estop_engaged_ ? "ENGAGED" : "CLEARED")
              << " at distance " << distance_cm << " cm" << std::endl;
}

bool UltrasonicNode::initGpio() {
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
        std::cerr << "[UltrasonicNode] ThreadCtl(_NTO_TCTL_IO) failed: " << strerror(errno) << std::endl;
        return false;
    }

    gpio_base_ = mmap_device_io(GPIO_REG_SPACE, BCM2711_GPIO_BASE);
    if (gpio_base_ == MAP_DEVICE_FAILED) {
        std::cerr << "[UltrasonicNode] mmap_device_io failed: " << strerror(errno) << std::endl;
        return false;
    }

    configureGpioMode(gpio_base_, ULTRASONIC_GPIO_TRIG, true);
    configureGpioMode(gpio_base_, ULTRASONIC_GPIO_ECHO, false);
    setTrig(false);
    return true;
}

bool UltrasonicNode::measureDistanceCm(uint32_t& distance_cm) {
    if (!setTrig(false)) return false;
    usleep(2);
    if (!setTrig(true)) return false;
    usleep(10);
    if (!setTrig(false)) return false;

    const uint64_t edge_timeout_us = 30000;

    int echo_level = 0;
    uint64_t wait_start = monotonicUs();
    while ((monotonicUs() - wait_start) < edge_timeout_us) {
        if (!readEchoLevel(echo_level)) return false;
        if (echo_level == 1) break;
    }
    if (echo_level != 1) return false;

    const uint64_t pulse_start = monotonicUs();

    while ((monotonicUs() - pulse_start) < edge_timeout_us) {
        if (!readEchoLevel(echo_level)) return false;
        if (echo_level == 0) break;
    }
    if (echo_level != 0) return false;

    const uint64_t pulse_end = monotonicUs();
    if (pulse_end <= pulse_start) return false;

    const uint64_t pulse_us = pulse_end - pulse_start;
    const double distance = static_cast<double>(pulse_us) / 58.0; // HC-SR04 cm approximation
    if (distance < static_cast<double>(ULTRASONIC_MIN_CM) ||
        distance > static_cast<double>(ULTRASONIC_MAX_CM)) {
        return false;
    }

    distance_cm = static_cast<uint32_t>(distance + 0.5);
    return true;
}

bool UltrasonicNode::readEchoLevel(int& level) {
    if (gpio_base_ == MAP_DEVICE_FAILED) return false;
    uint32_t value = in32(gpio_base_ + GPLEV0);
    level = (value & (1u << ULTRASONIC_GPIO_ECHO)) ? 1 : 0;
    return true;
}

bool UltrasonicNode::setTrig(bool high) {
    if (gpio_base_ == MAP_DEVICE_FAILED) return false;
    if (high) {
        out32(gpio_base_ + GPSET0, (1u << ULTRASONIC_GPIO_TRIG));
    } else {
        out32(gpio_base_ + GPCLR0, (1u << ULTRASONIC_GPIO_TRIG));
    }
    return true;
}

uint64_t UltrasonicNode::monotonicUs() const {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL +
           static_cast<uint64_t>(ts.tv_nsec / 1000ULL);
}

