#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdint.h>
#include <sys/time.h>
#include <cstdlib>

#define UART_DEVICE "/dev/ser1"

static int16_t le16(const uint8_t *p) {
    return (int16_t)((p[1] << 8) | p[0]);
}

static int setup_uart(int fd) {
    struct termios tio;

    if (tcgetattr(fd, &tio) < 0) return -1;

    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);

    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;

    tio.c_cc[VMIN]  = 1;
    tio.c_cc[VTIME] = 2;

    return tcsetattr(fd, TCSANOW, &tio);
}

static bool read_exact(int fd, uint8_t *buf, int len) {
    int n = 0;
    while (n < len) {
        int r = read(fd, buf + n, len - n);
        if (r <= 0) return false;
        n += r;
    }
    return true;
}

static bool bno_write_reg(int fd, uint8_t reg, uint8_t val) {
    uint8_t cmd[5] = {0xAA, 0x00, reg, 0x01, val};
    uint8_t resp[2];

    tcflush(fd, TCIOFLUSH);

    int wn = write(fd, cmd, 5);
    if (wn != 5) {
        std::cerr << "write failed, wn=" << wn << std::endl;
        return false;
    }

    tcdrain(fd);
    usleep(20000);

    if (!read_exact(fd, resp, 2)) {
        std::cerr << "short write response" << std::endl;
        return false;
    }

    std::cout << "write response: 0x"
              << std::hex << (int)resp[0] << " 0x" << (int)resp[1]
              << std::dec << std::endl;

    return (resp[0] == 0xEE && resp[1] == 0x01);
}

static bool bno_read_block(int fd, uint8_t reg, uint8_t len, uint8_t *data) {
    uint8_t cmd[4] = {0xAA, 0x01, reg, len};
    uint8_t hdr[2];

    tcflush(fd, TCIOFLUSH);

    if (write(fd, cmd, 4) != 4) {
        std::cerr << "read command write failed" << std::endl;
        return false;
    }

    tcdrain(fd);
    usleep(20000);

    if (!read_exact(fd, hdr, 2)) {
        std::cerr << "short read header" << std::endl;
        return false;
    }

    if (hdr[0] == 0xEE) {
        std::cerr << "sensor read error: 0x"
                  << std::hex << (int)hdr[1] << std::dec << std::endl;
        return false;
    }

    if (hdr[0] != 0xBB || hdr[1] != len) {
        std::cerr << "unexpected read header: 0x"
                  << std::hex << (int)hdr[0] << " 0x" << (int)hdr[1]
                  << std::dec << std::endl;
        return false;
    }

    if (!read_exact(fd, data, len)) {
        std::cerr << "short read data" << std::endl;
        return false;
    }

    return true;
}

int run_imu_test() {
//    std::cout << "Restarting serial driver..." << std::endl;
//    system("slay devc-serminiuart");
//    usleep(200000);
//    system("devc-serminiuart -b115200 -c480000000 -e -F -u1 0xfe215000,125 &");
//    usleep(500000);

    int fd = open(UART_DEVICE, O_RDWR);
    if (fd < 0) {
        std::cerr << "Failed to open " << UART_DEVICE << std::endl;
        return 1;
    }

    if (setup_uart(fd) < 0) {
        std::cerr << "UART setup failed" << std::endl;
        close(fd);
        return 1;
    }

    {
        uint8_t chip[1];
        std::cout << "Reading chip ID..." << std::endl;
        if (!bno_read_block(fd, 0x00, 1, chip)) {
            std::cerr << "Failed to read chip ID" << std::endl;
            close(fd);
            return 1;
        }

        std::cout << "chip id = 0x"
                  << std::hex << (int)chip[0]
                  << std::dec << " (expect 0xA0)" << std::endl;
    }

    std::cout << "Setting CONFIGMODE..." << std::endl;
    if (!bno_write_reg(fd, 0x3D, 0x00)) {
        std::cerr << "Failed to set CONFIGMODE" << std::endl;
        close(fd);
        return 1;
    }

    usleep(20000);

    std::cout << "Setting NDOF mode..." << std::endl;
    if (!bno_write_reg(fd, 0x3D, 0x0C)) {
        std::cerr << "Failed to set NDOF mode" << std::endl;
        close(fd);
        return 1;
    }

    usleep(20000);

    {
        uint8_t mode[1];
        std::cout << "Reading back OPR_MODE..." << std::endl;
        if (bno_read_block(fd, 0x3D, 1, mode)) {
            std::cout << "OPR_MODE = 0x"
                      << std::hex << (int)mode[0]
                      << std::dec << std::endl;
        } else {
            std::cout << "Could not read back OPR_MODE" << std::endl;
        }
    }

    sleep(1);

    std::cout << "Reading linear acceleration for 5 seconds..." << std::endl;

    struct timeval start, now;
    gettimeofday(&start, nullptr);

    while (true) {
        gettimeofday(&now, nullptr);

        double elapsed =
            (now.tv_sec - start.tv_sec) +
            (now.tv_usec - start.tv_usec) / 1000000.0;

        if (elapsed >= 5.0) break;

        uint8_t data[6];
        if (bno_read_block(fd, 0x28, 6, data)) {
            double ax = le16(&data[0]) / 100.0;
            double ay = le16(&data[2]) / 100.0;
            double az = le16(&data[4]) / 100.0;

            std::cout << "Accel: X=" << ax
                      << " Y=" << ay
                      << " Z=" << az
                      << " m/s^2" << std::endl;
        } else {
            std::cout << "Read failed" << std::endl;
        }

        usleep(250000);
    }

    close(fd);
    std::cout << "IMU test finished." << std::endl;
    return 0;
}
