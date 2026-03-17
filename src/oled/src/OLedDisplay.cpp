#include "../include/OLedDisplay.hpp"
#include "../../common/include/constants.h"
#include "../../common/include/ipc.h"
#include "../../common/include/priorities.h"

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <devctl.h>
#include <hw/i2c.h>
#include <stdexcept>
#include <cstring>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>

extern volatile sig_atomic_t g_shutdown_requested;

#define TIMER_PULSE_CODE (_PULSE_CODE_MINAVAIL + 1)
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

static size_t alignedMutexOffset() {
    const size_t a = alignof(pthread_mutex_t);
    const size_t n = sizeof(UltrasonicSharedState);
    return (n + (a - 1)) & ~(a - 1);
}

static size_t sharedMapSize() {
    return alignedMutexOffset() + sizeof(pthread_mutex_t);
}

// Standard 5x8 ASCII Font (Characters 32-127)
static const uint8_t font5x8[96][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00}, //   ! "
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62}, // # $ %
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00}, // & ' (
    {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08}, // ) * +
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00}, // , - .
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, // / 0 1
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10}, // 2 3 4
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, // 5 6 7
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00}, // 8 9 :
    {0x00,0x56,0x36,0x00,0x00}, {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14}, // ; < =
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E}, // > ? @
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22}, // A B C
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01}, // D E F
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, // G H I
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40}, // J K L
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E}, // M N O
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, // P Q R
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F}, // S T U
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63}, // V W X
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00}, // Y Z [
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04}, // \ ] ^
    {0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78}, // _ ` a
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F}, // b c d
    {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E}, // e f g
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00}, // h i j
    {0x7F,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78}, // k l m
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08}, // n o p
    {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20}, // q r s
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C}, // t u v
    {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C}, // w x y
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7F,0x00,0x00}, // z { |
    {0x00,0x41,0x36,0x08,0x00}, {0x10,0x08,0x08,0x10,0x08}, {0x00,0x00,0x00,0x00,0x00}  // } ~ (del)
};

OledDisplay::OledDisplay() 
    : i2c_fd_(-1),
      channel_(nullptr),
      timer_id_(-1),
      timer_(-1),
      running_(false),
      is_e_stopped_(true),
      ultrasonic_shm_fd_(-1),
      ultrasonic_state_(nullptr),
      ultrasonic_mutex_(nullptr),
      ultrasonic_available_(false),
      distance_cm_(0),
      distance_valid_(false) {
    
    memset(buffer_, 0, sizeof(buffer_));
}

OledDisplay::~OledDisplay() {
    stop();
    
    if (timer_ != -1) timer_delete(timer_);
    if (timer_id_ != -1) ConnectDetach(timer_id_);
    if (channel_ != nullptr) name_detach(channel_, 0);
    if (i2c_fd_ >= 0) close(i2c_fd_);

    if (ultrasonic_state_ != nullptr) {
        size_t map_size = sharedMapSize();
        munmap(ultrasonic_state_, map_size);
        ultrasonic_state_ = nullptr;
        ultrasonic_mutex_ = nullptr;
    }
    if (ultrasonic_shm_fd_ != -1) {
        close(ultrasonic_shm_fd_);
        ultrasonic_shm_fd_ = -1;
    }
}

bool OledDisplay::initI2C() {
    i2c_fd_ = open(I2C_DEVICE_PATH, O_RDWR);
    if (i2c_fd_ < 0) {
        std::cerr << "[OledDisplay] Failed to open I2C: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

void OledDisplay::writeCommand(uint8_t cmd) {
    struct {
        i2c_send_t hdr;
        uint8_t data[2];
    } msg;
    
    msg.hdr.slave.addr = SSD1306_I2C_ADDR;
    msg.hdr.slave.fmt = I2C_ADDRFMT_7BIT;
    msg.hdr.len = 2;
    msg.hdr.stop = 1;
    
    msg.data[0] = 0x00; // Co=0, D/C=0 (Command)
    msg.data[1] = cmd;
    
    devctl(i2c_fd_, DCMD_I2C_SEND, &msg, sizeof(msg), NULL);
}

void OledDisplay::writeData(uint8_t* data, size_t len) {
    // QNX devctl buffer limit is usually small. Send in 16-byte chunks.
    const size_t CHUNK_SIZE = 16;
    struct {
        i2c_send_t hdr;
        uint8_t buf[CHUNK_SIZE + 1]; // +1 for the control byte
    } msg;

    msg.hdr.slave.addr = SSD1306_I2C_ADDR;
    msg.hdr.slave.fmt = I2C_ADDRFMT_7BIT;
    msg.hdr.stop = 1;

    for (size_t i = 0; i < len; i += CHUNK_SIZE) {
        size_t current_chunk = std::min(CHUNK_SIZE, len - i);
        msg.hdr.len = current_chunk + 1;
        
        msg.buf[0] = 0x40; // Co=0, D/C=1 (Data)
        memcpy(&msg.buf[1], &data[i], current_chunk);
        
        devctl(i2c_fd_, DCMD_I2C_SEND, &msg, sizeof(msg.hdr) + current_chunk + 1, NULL);
    }
}

void OledDisplay::initOLED() {
    // Standard SSD1306 Init sequence
    uint8_t init_cmds[] = {
        0xAE, 0x20, 0x00, 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07,
        0x40, 0x81, 0x7F, 0xA1, 0xA6, 0xA8, 0x3F, 0xC8, 0xD3,
        0x00, 0xD5, 0x80, 0xD9, 0x22, 0xDA, 0x12, 0xDB, 0x20, 
        0x8D, 0x14, 0xAF
    };
    
    for(uint8_t cmd : init_cmds) {
        writeCommand(cmd);
    }
    clearBuffer();
    updateDisplay();
}

void OledDisplay::clearBuffer() {
    memset(buffer_, 0, sizeof(buffer_));
}

void OledDisplay::updateDisplay() {
    // Set column address
    writeCommand(0x21); writeCommand(0); writeCommand(127);
    // Set page address
    writeCommand(0x22); writeCommand(0); writeCommand(7);
    // Write full buffer
    writeData(buffer_, sizeof(buffer_));
}

// Add 'int size = 1' to the method signature in your OledDisplay.hpp file too!
void OledDisplay::drawChar(char c, int& x, int& y, int size) {
    // Fallback to '?' if the character isn't in our standard ASCII array
    if (c < 32 || c > 126) c = '?'; 
    int font_idx = c - 32;

    // Check if the character will overflow the screen width; if so, wrap to next line
    if (x + (6 * size) > OLED_WIDTH) { 
        x = 0;
        y += (8 * size);
    }
    if (y >= OLED_HEIGHT) return; // Out of vertical bounds

    // Render the 5 vertical columns of the font character
    for (int i = 0; i < 5; i++) {
        uint8_t line = font5x8[font_idx][i];
        
        // Read the 8 bits of the current column
        for (int j = 0; j < 8; j++, line >>= 1) {
            if (line & 1) { // If the pixel is "on"
                
                // Draw a square block of pixels depending on the 'size' parameter
                for (int sx = 0; sx < size; sx++) {
                    for (int sy = 0; sy < size; sy++) {
                        int px = x + (i * size) + sx;
                        int py = y + (j * size) + sy;
                        
                        // Ensure we don't draw outside the screen buffer
                        if (px < OLED_WIDTH && py < OLED_HEIGHT) {
                            int page = py / 8;
                            buffer_[page * OLED_WIDTH + px] |= (1 << (py % 8));
                        }
                    }
                }
            }
        }
    }
    // Advance the cursor forward (5 pixels width + 1 pixel spacing) * size
    x += 6 * size; 
}

void OledDisplay::drawStringWithWrap(const std::string& text, int x, int y, int size) {
    int curr_x = x;
    int curr_y = y;
    
    for (char c : text) {
        if (c == '\n') {
            curr_x = 0;
            curr_y += (8 * size);
            continue;
        }
        drawChar(c, curr_x, curr_y, size);
    }
}

void OledDisplay::renderUI() {
    clearBuffer();
    
    // --- TITLE HEADER ---
    // size = 1 (8 pixels high). 
    // We use \n to force a clean break before it wraps mid-word.
    // This takes up vertical space from y=0 to y=15.
    drawStringWithWrap("Group 7 QNX Car", 0, 0, 1);
    
    // --- E-STOP STATUS ---
    // Keep text compact to leave room for distance line.
    if (is_e_stopped_) {
        drawStringWithWrap("E-STOP: ON", 0, 20, 1);
    } else {
        drawStringWithWrap("E-STOP: OFF", 0, 20, 1);
    }

    std::string distance_text = "Distance : --- cm";
    if (distance_valid_) {
        distance_text = "Distance : " + std::to_string(distance_cm_) + " cm";
    }
    // Keep distance line small (same font as title) and place it under title.
    drawStringWithWrap(distance_text, 0, 10, 1);
    
    updateDisplay();
}
bool OledDisplay::start() {
    if (!initI2C()) return false;
    initOLED();
    
    channel_ = name_attach(NULL, IPC_OLED_CHANNEL, 0);
    if (!channel_) {
        std::cerr << "[OledDisplay] Failed to attach IPC channel." << std::endl;
        return false;
    }

    timer_id_ = ConnectAttach(0, 0, channel_->chid, _NTO_SIDE_CHANNEL, 0);
    SIGEV_PULSE_INIT(&timer_event_, timer_id_, PRIORITY_OLED_NODE, TIMER_PULSE_CODE, 0);
    timer_create(CLOCK_MONOTONIC, &timer_event_, &timer_);

    // 1.0 Second polling interval
    struct itimerspec timer_spec = {};
    timer_spec.it_value.tv_sec = 1;
    timer_spec.it_interval.tv_sec = 1;
    timer_settime(timer_, 0, &timer_spec, NULL);

    ultrasonic_shm_fd_ = shm_open(ULTRASONIC_SHM_NAME, O_RDWR, 0);
    if (ultrasonic_shm_fd_ == -1) {
        std::cerr << "[OledDisplay] Ultrasonic shared memory unavailable: " << strerror(errno) << std::endl;
        ultrasonic_available_ = false;
    } else {
        size_t map_size = sharedMapSize();
        void* ptr = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, ultrasonic_shm_fd_, 0);
        if (ptr == MAP_FAILED) {
            std::cerr << "[OledDisplay] Ultrasonic mmap failed: " << strerror(errno) << std::endl;
            close(ultrasonic_shm_fd_);
            ultrasonic_shm_fd_ = -1;
            ultrasonic_available_ = false;
        } else {
            ultrasonic_state_ = reinterpret_cast<UltrasonicSharedState*>(ptr);
            ultrasonic_mutex_ = reinterpret_cast<pthread_mutex_t*>(
                reinterpret_cast<char*>(ptr) + alignedMutexOffset());
            ultrasonic_available_ = true;
        }
    }

    running_ = true;
    renderUI(); // Initial draw

    union {
        struct _pulse pulse;
        EmergencyStopCommandMsg estop_msg;
        uint16_t type; // Quick access to the first 2 bytes for the type check
    } msg;

    std::cout << "[OledDisplay] Entering message loop (chid=" << channel_->chid << ")" << std::endl;

    while (running_ && !g_shutdown_requested) {
        int rcvid = MsgReceive(channel_->chid, &msg, sizeof(msg), NULL);
        
        if (rcvid == 0 && msg.pulse.code == TIMER_PULSE_CODE) {
            // std::cout << "[OledDisplay] Timer pulse received." << std::endl;
            processTimerPulse();
        } 
        else if (rcvid > 0) {
            uint16_t msg_type = msg.type; 
            
            if (msg_type == _IO_CONNECT) {
                // Acknowledge the QNX kernel handshake so name_open() succeeds
                MsgReply(rcvid, EOK, NULL, 0);
            }
            else if (msg_type == MSG_TYPE_EMERGENCY_STOP) {
                std::cout << "[OledDisplay] IPC message received (rcvid=" << rcvid << ", msg_type=" << msg_type << ")" << std::endl;
                handleEStop(msg.estop_msg.engage);
                MsgReply(rcvid, EOK, NULL, 0);
            } 
            else {
                std::cerr << "[OledDisplay] Unknown msg_type=" << msg_type << ", rejecting." << std::endl;
                MsgError(rcvid, EINVAL);
            }
        }
        else if (rcvid == -1) {
            if (errno == EINTR) {
                break;
            }
            std::cerr << "[OledDisplay] MsgReceive error: " << strerror(errno) << std::endl;
        }
    }
    
    // Clear screen on exit
    clearBuffer();
    updateDisplay();
    return true;
}

void OledDisplay::stop() {
    running_ = false;
}

void OledDisplay::processTimerPulse() {
    // Boot-up resilience: OLED may start before ultrasonic creates shared memory.
    // Keep retrying attach each pulse until it succeeds.
    if (!ultrasonic_available_ && ultrasonic_shm_fd_ == -1) {
        ultrasonic_shm_fd_ = shm_open(ULTRASONIC_SHM_NAME, O_RDWR, 0);
        if (ultrasonic_shm_fd_ != -1) {
            size_t map_size = sharedMapSize();
            void* ptr = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, ultrasonic_shm_fd_, 0);
            if (ptr == MAP_FAILED) {
                close(ultrasonic_shm_fd_);
                ultrasonic_shm_fd_ = -1;
            } else {
                ultrasonic_state_ = reinterpret_cast<UltrasonicSharedState*>(ptr);
                ultrasonic_mutex_ = reinterpret_cast<pthread_mutex_t*>(
                    reinterpret_cast<char*>(ptr) + alignedMutexOffset());
                ultrasonic_available_ = true;
                std::cout << "[OledDisplay] Connected to ultrasonic shared memory." << std::endl;
            }
        }
    }

    if (ultrasonic_available_ && ultrasonic_state_) {
        // Read a snapshot without locking to avoid blocking on cross-process
        // mutex state during boot/restarts. For this small struct, occasional
        // torn reads are acceptable and quickly corrected next pulse.
        distance_valid_ = (ultrasonic_state_->valid != 0);
        if (distance_valid_) {
            distance_cm_ = ultrasonic_state_->last_distance_cm;
        }
    } else {
        distance_valid_ = false;
    }

    static int oled_debug_counter = 0;
    oled_debug_counter++;
    if (oled_debug_counter >= 2) { // every ~2 seconds
        oled_debug_counter = 0;
        if (distance_valid_) {
            std::cout << "[OledDisplay] Drawing Distance : " << distance_cm_ << " cm" << std::endl;
        } else {
            std::cout << "[OledDisplay] Drawing Distance : --- cm" << std::endl;
        }
    }

    renderUI();
}

void OledDisplay::handleEStop(bool engaged) {
    std::cout << "[OledDisplay] handleEStop: engaged=" << engaged << " (was " << is_e_stopped_ << ")" << std::endl;
    if (is_e_stopped_ != engaged) {
        is_e_stopped_ = engaged;
        std::cout << "[OledDisplay] E-Stop state changed, re-rendering UI." << std::endl;
        renderUI();
    }
}