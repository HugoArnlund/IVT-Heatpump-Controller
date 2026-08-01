#include "IRController.h"

void IRController::send(uint8_t power, uint8_t tenMode, uint8_t fanSpeed, uint8_t temperature)
{
  IRSenderESP8266 IR(5);

  //uint8_t SharpTemplate[] = { 0xAA, 0x5A, 0xCF, 0x10, 0x03, 0x21, 0x21, 0x0E, 0x08, 0x80, 0x04, 0xF0, 0xB1 }; // Heat 20 deg auto fan
  //uint8_t SharpTemplate[] = {0xAA, 0x5A, 0xCF, 0x10, 0x03, 0x11, 0x21, 0x0F, 0x08, 0x80, 0x00, 0xF0, 0x01};
  //                             0     1     2     3     4     5     6     7     8     9    10    11    12

  uint8_t SharpTemplate[] = { 0xAA, 0x5A, 0xCF, 0x10, 0x00, 0x11, 0x21, 0x0E, 0x08, 0x80, 0x04, 0xF0, 0x01 };


  if( power == 0 ) { // Power off
    SharpTemplate[5] = 0x21; // Power off
  } else {
    SharpTemplate[4] = 0x01 + (temperature - 18); // Change temperature

    uint8_t fanSpeeds[] = { 0x21, 0x31, 0x51, 0x71 }; // Auto, low, med, high
    SharpTemplate[6] = fanSpeeds[fanSpeed]; // Set fan speed
  }

  if ( tenMode == 1 ) {
    send(1,0,3,20); // Send 20 degrees to start the heatpump

    // Turn on 10 degree mode
    uint8_t TenModeTemplate[] = {0xAA, 0x5A, 0xCF, 0x10, 0x00, 0x31, 0x71, 0x0B, 0x08, 0x80, 0x05, 0xF0, 0x91};
    memcpy(SharpTemplate, TenModeTemplate, sizeof(SharpTemplate));
  } 

  uint8_t checksum = 0x00;


  // Calculate the checksum
  for (int i=0; i<12; i++) {
    checksum ^= SharpTemplate[i];
  }

  checksum ^= SharpTemplate[12] & 0x0F;
  checksum ^= (checksum >> 4);
  checksum &= 0x0F;

  SharpTemplate[12] |= (checksum << 4);


  for(int i = 0; i < 3; i++) { // Send 5 times
    // 38 kHz PWM frequency
    IR.setFrequency(38);

    // Header
    IR.mark(SHARP_AIRCON1_HDR_MARK);
    IR.space(SHARP_AIRCON1_HDR_SPACE);

    // Data
    for (unsigned int i=0; i<sizeof(SharpTemplate); i++) {
      IR.sendIRbyte(SharpTemplate[i], SHARP_AIRCON1_BIT_MARK, SHARP_AIRCON1_ZERO_SPACE, SHARP_AIRCON1_ONE_SPACE);
    }

    // End mark
    IR.mark(SHARP_AIRCON1_BIT_MARK);
    IR.space(0);

    delay(1000);
  }

  for (int i = 0; i < sizeof(SharpTemplate); ++i) {
    Serial.print("0x");
    Serial.print(static_cast<int>(SharpTemplate[i]), HEX);
    Serial.print(" ");
  }
  Serial.print("\n");
}

