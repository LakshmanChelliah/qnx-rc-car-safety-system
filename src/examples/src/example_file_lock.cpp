#include <iostream>
#include <unistd.h>
#include <sys/dispatch.h> // Required for QNX native name_attach

int main() {
    std::cout << "--- QNX Native Lock Tester ---" << std::endl;

    std::cout << "Attempting to acquire exclusive QNX IPC lock..." << std::endl;

    // 1. Try to register a unique global name with the QNX Process Manager
    name_attach_t* lock_name = name_attach(NULL, "adeept_motor_lock", 0);
    
    // 2. If it returns NULL, the name is already taken by another running process
    if (lock_name == NULL) {
        std::cerr << "LOCKED: Another instance is currently holding the lock!" << std::endl;
        return -1;
    }

    // 3. Lock acquired successfully!
    std::cout << "SUCCESS: Lock acquired!" << std::endl;
    std::cout << "Holding lock for 15 seconds. Quickly open another SSH terminal and run this program again to see it fail..." << std::endl;

    // Simulate work by sleeping and counting down
    for(int i = 15; i > 0; i--) {
        std::cout << i << " seconds remaining..." << std::endl;
        sleep(1);
    }

    // 4. Release the lock (The OS will also do this automatically if the program crashes!)
    std::cout << "Releasing lock..." << std::endl;
    name_detach(lock_name, 0);

    std::cout << "Lock released. Exiting cleanly." << std::endl;
    return 0;
}