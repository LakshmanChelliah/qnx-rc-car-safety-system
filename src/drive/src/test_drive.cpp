#include <iostream>
#include <cstring>
#include <unistd.h>
#include <errno.h>
#include <sys/dispatch.h>
#include "../../common/include/ipc.h"

int main() {
    std::cout << "=== QNX Drive Controller Test ===" << std::endl;
    std::cout << "Connecting to drive controller..." << std::endl;
    
    // Connect to the drive controller server
    int server_coid = name_open(IPC_DRIVE_CHANNEL, 0);
    if (server_coid == -1) {
        std::cerr << "ERROR: Failed to connect to '" << IPC_DRIVE_CHANNEL 
                  << "'. Error: " << strerror(errno) << std::endl;
        std::cerr << "Make sure the drive controller is running!" << std::endl;
        return 1;
    }
    
    std::cout << "Connected successfully!" << std::endl;
    
    // Step 1: Set active control source to SOURCE_USB_JOYSTICK
    std::cout << "\n[1/4] Setting active control source to SOURCE_USB_JOYSTICK..." << std::endl;
    DriveControlCommandMsg control_msg;
    control_msg.msg_type = MSG_TYPE_DRIVE_CONTROL;
    control_msg.new_source = SOURCE_USB_JOYSTICK;
    
    int result = MsgSend(server_coid, &control_msg, sizeof(control_msg), NULL, 0);
    if (result == -1) {
        std::cerr << "ERROR: Failed to set control source: " << strerror(errno) << std::endl;
        name_close(server_coid);
        return 1;
    }
    std::cout << "Control source set successfully." << std::endl;
    
    // Step 2: Run forward/reverse test 3 times
    DriveSpeedCommandMsg speed_msg;
    speed_msg.msg_type = MSG_TYPE_DRIVE_SPEED;
    speed_msg.source = SOURCE_USB_JOYSTICK;
    
    for (int i = 1; i <= 3; i++) {
        std::cout << "\n[Cycle " << i << "/3]" << std::endl;
        
        // Forward at 30% speed
        std::cout << "  Forward (0.3, 0.3)..." << std::endl;
        speed_msg.left_speed = 0.3f;
        speed_msg.right_speed = 0.3f;
        
        result = MsgSend(server_coid, &speed_msg, sizeof(speed_msg), NULL, 0);
        if (result == -1) {
            std::cerr << "ERROR: Failed to send forward command: " << strerror(errno) << std::endl;
            break;
        }
        
        usleep(300000); // 0.3 seconds
        
        // Reverse at 30% speed
        std::cout << "  Reverse (-0.3, -0.3)..." << std::endl;
        speed_msg.left_speed = -0.3f;
        speed_msg.right_speed = -0.3f;
        
        result = MsgSend(server_coid, &speed_msg, sizeof(speed_msg), NULL, 0);
        if (result == -1) {
            std::cerr << "ERROR: Failed to send reverse command: " << strerror(errno) << std::endl;
            break;
        }
        
        usleep(300000); // 0.3 seconds
    }
    
    // Step 3: Stop motors
    std::cout << "\n[4/4] Stopping motors..." << std::endl;
    speed_msg.left_speed = 0.0f;
    speed_msg.right_speed = 0.0f;
    
    result = MsgSend(server_coid, &speed_msg, sizeof(speed_msg), NULL, 0);
    if (result == -1) {
        std::cerr << "ERROR: Failed to send stop command: " << strerror(errno) << std::endl;
    } else {
        std::cout << "Motors stopped." << std::endl;
    }
    
    // Step 4: Disconnect
    std::cout << "\nDisconnecting..." << std::endl;
    name_close(server_coid);
    
    std::cout << "=== Test Complete ===" << std::endl;
    return 0;
}
