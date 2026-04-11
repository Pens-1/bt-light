#pragma once

#include <cstdint>
#include "config.h"

class LampController {
public:
    void begin();
    void pair();
    void unpair();
    void on();
    void off();
    void setBrightness(uint8_t cold, uint8_t warm);
    void setRGB(uint8_t r, uint8_t g, uint8_t b);
    void nightLight();

private:
    void sendCommand(uint8_t cmd, uint8_t arg0 = 0, uint8_t arg1 = 0, uint8_t arg2 = 0, int repeat = BLE_ADV_REPEAT);
    void sendRGBCommand(uint8_t r, uint8_t g, uint8_t b);
};
