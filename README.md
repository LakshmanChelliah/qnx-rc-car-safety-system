# qnx-rc-car-safety-system
A QNX-based real-time safety system for a Raspberry Pi–powered RC car. The project incrementally adds adaptive cruise control, crash detection, and collision avoidance to demonstrate deterministic scheduling, IPC, and safety-critical RTOS design in an automotive-inspired environment. For COMP4900E


## RC Car Wifi Router Details
- The router publishes a network with the name `COMP4900E_Group7` with password qnxuser123.
- The raspberry pi 4b running qnx is connected to the router via ethernet.
- The pi has a static ip of 192.168.7.2, a username of `qnxuser` and a password of `qnxuser`.
- Can ssh with `ssh qnxuser@192.168.7.2` then password `qnxuser`.
- Or copy a binary to it with `scp src/drive/build/aarch64le-debug/drive.aarch64le.bin qnxuser@192.168.7.2:/tmp`
- src/ultrasonic/src/UltrasonicNode.cpp [estop_threshold_cm: change distance till power cut]