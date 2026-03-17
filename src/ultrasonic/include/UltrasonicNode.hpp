#ifndef ULTRASONIC_NODE_HPP
#define ULTRASONIC_NODE_HPP

#include "../../common/include/ultrasonic.h"

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
    bool running_;

    bool initSharedState();
    void cleanupSharedState();

    bool initGpio();
    bool measureDistanceCm(uint32_t& distance_cm);
    bool readEchoLevel(int& level);
    bool setTrig(bool high);

    uint64_t monotonicUs() const;
};

#endif // ULTRASONIC_NODE_HPP

