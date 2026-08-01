#ifndef IRCONTROLLER_H
#define IRCONTROLLER_H

#include <IRSender.h>

class IRController {
public:

    void send(uint8_t power, uint8_t tenMode, uint8_t fanSpeed, uint8_t temperature);

    #define SHARP_AIRCON1_HDR_MARK   3540 // 3540 // 3820
    #define SHARP_AIRCON1_HDR_SPACE  1720 // 1720 // 1680
    #define SHARP_AIRCON1_BIT_MARK   460 //460  // 420
    #define SHARP_AIRCON1_ONE_SPACE  1400 //1400 // 1250
    #define SHARP_AIRCON1_ZERO_SPACE 430 //430  // 330
};
#endif // IRCONTROLLER_H