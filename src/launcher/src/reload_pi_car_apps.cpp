#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

#include "../../common/include/executables.hpp"

int main(int argc, char *argv[]) {
    bool include_drive = false;
    bool skip_slay = false;
    bool logs = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--drive") {
            include_drive = true;
        } else if (arg == "--skip-slay") {
            skip_slay = true;
        } else if (arg == "--logs") {
            logs = true;
        }
    }

    std::cout << "Launcher binary executed. Constructing reload sequence..." << std::endl;

    // Start building the command
    std::string command = "chmod +x /fs/apps/reload_pi_car_apps.sh && /fs/apps/reload_pi_car_apps.sh";

    // Pass optional flags to the shell script
    if (logs) {
        command += " --logs";
    }
    if (skip_slay) {
        command += " --skip-slay";
    }

    // Append standard managed apps from the header
    for (const auto& app : MANAGED_APPS) {
        command += " " + app;
    }

    // Conditionally append the drive executable
    if (include_drive) {
        command += " " + DRIVE_APP;
    }

    std::cout << "Executing: " << command << std::endl;
    
    // Execute the shell script with the constructed arguments
    int result = system(command.c_str());

    return result;
}