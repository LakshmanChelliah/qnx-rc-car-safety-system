#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdint.h>
#include <sys/time.h>
#include <cstdlib>
#include <csignal>
#include <cmath>
#include <ctime>

#define UART_DEVICE "/dev/ser1"

// --- Timing ---
#define SAMPLE_PERIOD_US 10000   // 10 ms sampling
#define SENSOR_RESPONSE_US 5000

// --- Crash detection tuning ---
#define VTHRD 10.0
#define CONFIRM_MS 35
#define CRASH_COOLDOWN_MS 1000   // prevent repeated logs for one impact

static const char* PRIMARY_LOG_PATH = "/fs/apps/crash_detection.log";
static const char* FALLBACK_LOG_PATH = "/tmp/crash_detection.log";

volatile sig_atomic_t g_stop = 0;
static double g_program_start_time = 0.0;

static void handle_sigint(int) {
    g_stop = 1;
}

static int16_t le16(const uint8_t *p) {
    return (int16_t)((p[1] << 8) | p[0]);
}

static double now_seconds() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static std::string uptime_timestamp() {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3)
        << (now_seconds() - g_program_start_time) << "s";
    return oss.str();
}

static std::string current_timestamp() {
    std::time_t now = std::time(nullptr);
    struct tm local_tm;
    localtime_r(&now, &local_tm);

    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local_tm);
    return std::string(buf);
}

static void append_log_line(const std::string& line) {
    std::ofstream log(PRIMARY_LOG_PATH, std::ios::app);
    if (!log.is_open()) {
        log.open(FALLBACK_LOG_PATH, std::ios::app);
    }
    if (!log.is_open()) {
        return;
    }

    log << line << '\n';
    log.flush();
}

static void log_event(const std::string& message) {
    std::ostringstream oss;
    oss << "[wall " << current_timestamp() << "] "
        << "[uptime " << uptime_timestamp() << "] "
        << message;
    append_log_line(oss.str());
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
    tio.c_cc[VTIME] = 1;

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
        return false;
    }

    tcdrain(fd);
    usleep(SENSOR_RESPONSE_US);

    if (!read_exact(fd, resp, 2)) {
        return false;
    }

    return (resp[0] == 0xEE && resp[1] == 0x01);
}

static bool bno_read_block(int fd, uint8_t reg, uint8_t len, uint8_t *data) {
    uint8_t cmd[4] = {0xAA, 0x01, reg, len};
    uint8_t hdr[2];

    tcflush(fd, TCIOFLUSH);

    if (write(fd, cmd, 4) != 4) {
        return false;
    }

    tcdrain(fd);
    usleep(SENSOR_RESPONSE_US);

    if (!read_exact(fd, hdr, 2)) {
        return false;
    }

    if (hdr[0] == 0xEE) {
        return false;
    }

    if (hdr[0] != 0xBB || hdr[1] != len) {
        return false;
    }

    if (!read_exact(fd, data, len)) {
        return false;
    }

    return true;
}

static const char* classify_impact_from(double ax, double ay) {
    // Based on your testing:
    // forward movement  -> negative Y
    // backward movement -> positive Y
    // bonk left  -> positive X
    // bonk right -> negative X
    if (std::fabs(ax) > std::fabs(ay)) {
        return (ax > 0.0) ? "Left" : "Right";
    } else {
        return (ay > 0.0) ? "Back" : "Front";
    }
}

int run_crash_detector() {
    std::signal(SIGINT, handle_sigint);
    std::signal(SIGTERM, handle_sigint);

    log_event("Crash detection service starting.");

    system("slay devc-serminiuart");
    usleep(200000);
    system("devc-serminiuart -b115200 -c480000000 -e -F -u1 0xfe215000,125 &");
    usleep(500000);

    int fd = open(UART_DEVICE, O_RDWR);
    if (fd < 0) {
        log_event("ERROR: Failed to open /dev/ser1.");
        return 1;
    }

    if (setup_uart(fd) < 0) {
        log_event("ERROR: UART setup failed.");
        close(fd);
        return 1;
    }

    {
        uint8_t chip[1];
        if (!bno_read_block(fd, 0x00, 1, chip)) {
            log_event("ERROR: Failed to read IMU chip ID.");
            close(fd);
            return 1;
        }

        if (chip[0] != 0xA0) {
            std::ostringstream oss;
            oss << "ERROR: Unexpected IMU chip ID 0x"
                << std::hex << static_cast<int>(chip[0]);
            log_event(oss.str());
            close(fd);
            return 1;
        }
    }

    if (!bno_write_reg(fd, 0x3D, 0x00)) {
        log_event("ERROR: Failed to set CONFIGMODE.");
        close(fd);
        return 1;
    }

    usleep(20000);

    if (!bno_write_reg(fd, 0x3D, 0x0C)) {
        log_event("ERROR: Failed to set NDOF mode.");
        close(fd);
        return 1;
    }

    usleep(20000);
    sleep(1);

    log_event("IMU initialized successfully. Monitoring for crashes.");

    double candidate_start_time = 0.0;
    double last_logged_crash_time = -1000.0;

    bool have_prev = false;
    bool candidate_active = false;

    double prev_ax = 0.0, prev_ay = 0.0, prev_az = 0.0;

    while (!g_stop) {
        double loop_now = now_seconds();

        uint8_t data[6];
        if (bno_read_block(fd, 0x28, 6, data)) {
            double current_ax = le16(&data[0]) / 100.0;
            double current_ay = le16(&data[2]) / 100.0;
            double current_az = le16(&data[4]) / 100.0;

            if (have_prev) {
                double dx = current_ax - prev_ax;
                double dy = current_ay - prev_ay;
                double dz = current_az - prev_az;

                double dmag = std::sqrt(dx * dx + dy * dy + dz * dz);

                if (dmag > VTHRD) {
                    if (!candidate_active) {
                        candidate_active = true;
                        candidate_start_time = loop_now;
                    }

                    double candidate_ms = (loop_now - candidate_start_time) * 1000.0;
                    double cooldown_ms = (loop_now - last_logged_crash_time) * 1000.0;

                    if (candidate_ms >= CONFIRM_MS && cooldown_ms >= CRASH_COOLDOWN_MS) {
                        const char* impact_from = classify_impact_from(current_ax, current_ay);

                        std::ostringstream oss;
                        oss << "Crash detected from " << impact_from
                            << " | ax=" << current_ax
                            << " ay=" << current_ay
                            << " az=" << current_az
                            << " dmag=" << dmag;
                        log_event(oss.str());

                        last_logged_crash_time = loop_now;
                        candidate_active = false;
                        candidate_start_time = 0.0;
                    }
                } else {
                    candidate_active = false;
                    candidate_start_time = 0.0;
                }
            }

            prev_ax = current_ax;
            prev_ay = current_ay;
            prev_az = current_az;
            have_prev = true;
        }

        usleep(SAMPLE_PERIOD_US);
    }

    close(fd);
    log_event("Crash detection service stopping.");
    return 0;
}

int main(int argc, char* argv[]) {
	g_program_start_time = now_seconds();
    return run_crash_detector();
}
