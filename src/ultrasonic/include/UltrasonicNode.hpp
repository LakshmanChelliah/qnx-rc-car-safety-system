#ifndef ULTRASONIC_NODE_HPP
#define ULTRASONIC_NODE_HPP

#include "../../common/include/ultrasonic.h"
#include "../../common/include/ipc.h"

#include <pthread.h>

class UltrasonicNode {
public:
    UltrasonicNode();
    ~UltrasonicNode();

    bool start();
    void stop();

private:
    int shm_fd_;
    UltrasonicSharedState* shared_state_;
    pthread_mutex_t* shared_mutex_;
    uintptr_t gpio_base_;
    int drive_coid_;
    bool estop_engaged_;
    bool running_;

    bool initSharedState();
    void cleanupSharedState();
    bool connectDriveIpc();
    void updateEmergencyStop(bool distance_valid, uint32_t distance_cm);

    bool initGpio();
    bool measureDistanceCm(uint32_t& distance_cm);
    bool readEchoLevel(int& level);
    bool setTrig(bool high);

    uint64_t monotonicUs() const;
};

#endif // ULTRASONIC_NODE_HPP

