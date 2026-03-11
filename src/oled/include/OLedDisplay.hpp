#ifndef OLED_DISPLAY_HPP
#define OLED_DISPLAY_HPP

#include <stdint.h>
#include <string>
#include <sys/siginfo.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>

class OledDisplay {
public:
    OledDisplay();
    ~OledDisplay();
    
    bool start();
    void stop();

private:
    // I2C & Hardware functions
    bool initI2C();
    void initOLED();
    void writeCommand(uint8_t cmd);
    void writeData(uint8_t* data, size_t len);
    
    // Graphics & Text functions
    void clearBuffer();
    void updateDisplay();
    void drawChar(char c, int& x, int& y, int size = 1);
    void drawStringWithWrap(const std::string& text, int x, int y, int size = 1);
    void renderUI();

    // IPC & Timer functions
    void processTimerPulse();
    void handleEStop(bool engaged);

    int i2c_fd_;
    name_attach_t* channel_;
    int timer_id_;
    timer_t timer_;
    struct sigevent timer_event_;
    
    bool running_;
    bool is_e_stopped_;
    
    // 128x64 display = 8 pages of 128 columns
    uint8_t buffer_[1024]; 
};

#endif // OLED_DISPLAY_HPP