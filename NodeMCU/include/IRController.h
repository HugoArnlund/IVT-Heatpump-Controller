#ifndef IRCONTROLLER_H
#define IRCONTROLLER_H

#include <IRSender.h>
#include <stdint.h>

namespace IRController {
    void send(uint8_t power, uint8_t tenMode, uint8_t fanSpeed, uint8_t temperature);
}

#endif // IRCONTROLLER_H