//#include <iostream>
//#include <fstream>
//#include <unistd.h>
//#include <fcntl.h>
//#include <termios.h>
//#include <stdint.h>
//#include <sys/time.h>
//#include <cstdlib>
//#include <csignal>
//#include <iomanip>
//#include <cmath>
//
//#define UART_DEVICE "/dev/ser1"
//
//// --- Timing ---
//#define SAMPLE_PERIOD_US 10000   // 10 ms sampling
//#define SENSOR_RESPONSE_US 5000
//
//// --- Crash detection tuning ---
//#define VTHRD 10.0
//#define CONFIRM_MS 50
//
//volatile sig_atomic_t g_stop = 0;
//
//static void handle_sigint(int) {
//    g_stop = 1;
//}
//
//static int16_t le16(const uint8_t *p) {
//    return (int16_t)((p[1] << 8) | p[0]);
//}
//
//static double now_seconds() {
//    struct timeval tv;
//    gettimeofday(&tv, nullptr);
//    return tv.tv_sec + tv.tv_usec / 1000000.0;
//}
//
//static void init_terminal_ui() {
//    std::cout << "\x1b[2J\x1b[H";
//    std::cout.flush();
//}
//
//static void update_terminal_ui(double elapsed,
//                               double ax, double ay, double az,
//                               double dmag,
//                               bool candidate_active,
//                               double candidate_ms,
//                               bool crash_detected,
//                               const char* impact_from,
//                               bool read_ok) {
//    std::cout << "\x1b[H";
//
//    std::cout << "IMU Crash Detection\n";
//    std::cout << "Sampling: 10 ms\n";
//    std::cout << "Press Ctrl+C to stop, or wait 60 seconds.\n\n";
//
//    std::cout << "Elapsed: " << std::fixed << std::setprecision(2)
//              << elapsed << " s\n\n";
//
//    if (read_ok) {
//        std::cout << "Accel: X=" << std::fixed << std::setprecision(2) << ax
//                  << "  Y=" << ay
//                  << "  Z=" << az
//                  << " m/s^2\n";
//
//        std::cout << "Delta magnitude: " << std::fixed << std::setprecision(3)
//                  << dmag << "\n";
//
//        std::cout << "Candidate: " << (candidate_active ? "YES" : "NO")
//                  << "   Candidate time: " << std::fixed << std::setprecision(0)
//                  << candidate_ms << " ms\n\n";
//    } else {
//        std::cout << "Read failed\n\n";
//    }
//
//    if (crash_detected) {
//        std::cout << "Crash Detected!\n";
//        std::cout << "Impact from: " << impact_from << "\n";
//    } else {
//        std::cout << "Crash Detected!: NO\n";
//    }
//
//    std::cout << "\x1b[J";
//    std::cout.flush();
//}
//
//static int setup_uart(int fd) {
//    struct termios tio;
//
//    if (tcgetattr(fd, &tio) < 0) return -1;
//
//    cfmakeraw(&tio);
//    cfsetispeed(&tio, B115200);
//    cfsetospeed(&tio, B115200);
//
//    tio.c_cflag |= (CLOCAL | CREAD);
//    tio.c_cflag &= ~PARENB;
//    tio.c_cflag &= ~CSTOPB;
//    tio.c_cflag &= ~CSIZE;
//    tio.c_cflag |= CS8;
//
//    tio.c_cc[VMIN]  = 1;
//    tio.c_cc[VTIME] = 1;
//
//    return tcsetattr(fd, TCSANOW, &tio);
//}
//
//static bool read_exact(int fd, uint8_t *buf, int len) {
//    int n = 0;
//    while (n < len) {
//        int r = read(fd, buf + n, len - n);
//        if (r <= 0) return false;
//        n += r;
//    }
//    return true;
//}
//
//static bool bno_write_reg(int fd, uint8_t reg, uint8_t val) {
//    uint8_t cmd[5] = {0xAA, 0x00, reg, 0x01, val};
//    uint8_t resp[2];
//
//    tcflush(fd, TCIOFLUSH);
//
//    int wn = write(fd, cmd, 5);
//    if (wn != 5) {
//        std::cerr << "write failed, wn=" << wn << std::endl;
//        return false;
//    }
//
//    tcdrain(fd);
//    usleep(SENSOR_RESPONSE_US);
//
//    if (!read_exact(fd, resp, 2)) {
//        std::cerr << "short write response" << std::endl;
//        return false;
//    }
//
//    std::cout << "write response: 0x"
//              << std::hex << (int)resp[0] << " 0x" << (int)resp[1]
//              << std::dec << std::endl;
//
//    return (resp[0] == 0xEE && resp[1] == 0x01);
//}
//
//static bool bno_read_block(int fd, uint8_t reg, uint8_t len, uint8_t *data) {
//    uint8_t cmd[4] = {0xAA, 0x01, reg, len};
//    uint8_t hdr[2];
//
//    tcflush(fd, TCIOFLUSH);
//
//    if (write(fd, cmd, 4) != 4) {
//        std::cerr << "read command write failed" << std::endl;
//        return false;
//    }
//
//    tcdrain(fd);
//    usleep(SENSOR_RESPONSE_US);
//
//    if (!read_exact(fd, hdr, 2)) {
//        std::cerr << "short read header" << std::endl;
//        return false;
//    }
//
//    if (hdr[0] == 0xEE) {
//        std::cerr << "sensor read error: 0x"
//                  << std::hex << (int)hdr[1] << std::dec << std::endl;
//        return false;
//    }
//
//    if (hdr[0] != 0xBB || hdr[1] != len) {
//        std::cerr << "unexpected read header: 0x"
//                  << std::hex << (int)hdr[0] << " 0x" << (int)hdr[1]
//                  << std::dec << std::endl;
//        return false;
//    }
//
//    if (!read_exact(fd, data, len)) {
//        std::cerr << "short read data" << std::endl;
//        return false;
//    }
//
//    return true;
//}
//
//static const char* classify_impact_from(double ax, double ay) {
//    // forward movement  -> negative Y
//    // backward movement -> positive Y
//    // bonk left  -> positive X
//    // bonk right -> negative X
//    //
//    // "Impact from" corresponds to the side being hit.
//    if (std::fabs(ax) > std::fabs(ay)) {
//        if (ax > 0.0) return "Left";
//        return "Right";
//    } else {
//        if (ay > 0.0) return "Back";
//        return "Front";
//    }
//}
//
//int run_imu_test() {
//    std::signal(SIGINT, handle_sigint);
//
//    std::cout << "Restarting serial driver..." << std::endl;
//    system("slay devc-serminiuart");
//    usleep(200000);
//    system("devc-serminiuart -b115200 -c480000000 -e -F -u1 0xfe215000,125 &");
//    usleep(500000);
//
//    int fd = open(UART_DEVICE, O_RDWR);
//    if (fd < 0) {
//        std::cerr << "Failed to open " << UART_DEVICE << std::endl;
//        return 1;
//    }
//
//    if (setup_uart(fd) < 0) {
//        std::cerr << "UART setup failed" << std::endl;
//        close(fd);
//        return 1;
//    }
//
//    {
//        uint8_t chip[1];
//        std::cout << "Reading chip ID..." << std::endl;
//        if (!bno_read_block(fd, 0x00, 1, chip)) {
//            std::cerr << "Failed to read chip ID" << std::endl;
//            close(fd);
//            return 1;
//        }
//
//        std::cout << "chip id = 0x"
//                  << std::hex << (int)chip[0]
//                  << std::dec << " (expect 0xA0)" << std::endl;
//    }
//
//    std::cout << "Setting CONFIGMODE..." << std::endl;
//    if (!bno_write_reg(fd, 0x3D, 0x00)) {
//        std::cerr << "Failed to set CONFIGMODE" << std::endl;
//        close(fd);
//        return 1;
//    }
//
//    usleep(20000);
//
//    std::cout << "Setting NDOF mode..." << std::endl;
//    if (!bno_write_reg(fd, 0x3D, 0x0C)) {
//        std::cerr << "Failed to set NDOF mode" << std::endl;
//        close(fd);
//        return 1;
//    }
//
//    usleep(20000);
//
//    {
//        uint8_t mode[1];
//        std::cout << "Reading back OPR_MODE..." << std::endl;
//        if (bno_read_block(fd, 0x3D, 1, mode)) {
//            std::cout << "OPR_MODE = 0x"
//                      << std::hex << (int)mode[0]
//                      << std::dec << std::endl;
//        } else {
//            std::cout << "Could not read back OPR_MODE" << std::endl;
//        }
//    }
//
//    sleep(1);
//    init_terminal_ui();
//
//    double start_time = now_seconds();
//    double candidate_start_time = 0.0;
//
//    bool have_prev = false;
//    bool candidate_active = false;
//
//    bool crash_detected = false;
//    const char* latched_impact_from = "Unknown";
//
//    double prev_ax = 0.0, prev_ay = 0.0, prev_az = 0.0;
//    double current_ax = 0.0, current_ay = 0.0, current_az = 0.0;
//    double dmag = 0.0;
//
//    while (!g_stop) {
//        double loop_now = now_seconds();
//        double elapsed = loop_now - start_time;
//
//        if (elapsed >= 60.0) break;
//
//        uint8_t data[6];
//        bool ok = false;
//
//        if (bno_read_block(fd, 0x28, 6, data)) {
//            current_ax = le16(&data[0]) / 100.0;
//            current_ay = le16(&data[2]) / 100.0;
//            current_az = le16(&data[4]) / 100.0;
//            ok = true;
//
//            if (have_prev) {
//                double dx = current_ax - prev_ax;
//                double dy = current_ay - prev_ay;
//                double dz = current_az - prev_az;
//
//                dmag = std::sqrt(dx * dx + dy * dy + dz * dz);
//
//                if (dmag > VTHRD) {
//                    if (!candidate_active) {
//                        candidate_active = true;
//                        candidate_start_time = loop_now;
//                    }
//
//                    double candidate_ms = (loop_now - candidate_start_time) * 1000.0;
//                    if (candidate_ms >= CONFIRM_MS) {
//                        crash_detected = true;
//                        latched_impact_from = classify_impact_from(current_ax, current_ay);
//                    }
//                } else {
//                    candidate_active = false;
//                    candidate_start_time = 0.0;
//                }
//            }
//
//            prev_ax = current_ax;
//            prev_ay = current_ay;
//            prev_az = current_az;
//            have_prev = true;
//        }
//
//        double candidate_ms = 0.0;
//        if (candidate_active) {
//            candidate_ms = (loop_now - candidate_start_time) * 1000.0;
//        }
//
//        update_terminal_ui(elapsed,
//                           current_ax, current_ay, current_az,
//                           dmag,
//                           candidate_active,
//                           candidate_ms,
//                           crash_detected,
//                           latched_impact_from,
//                           ok);
//
//        usleep(SAMPLE_PERIOD_US);
//    }
//
//    close(fd);
//
//    std::cout << "\nStopping IMU test.\n";
//    if (g_stop) {
//        std::cout << "Reason: Ctrl+C\n";
//    } else {
//        std::cout << "Reason: 60 second timeout\n";
//    }
//
//    return 0;
//}
//
