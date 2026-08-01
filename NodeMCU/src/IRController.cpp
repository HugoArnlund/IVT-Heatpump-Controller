#pragma once
#include <ESPLogger.h>
#include "IRController.h"

IRSenderESP8266 IR(5);
static constexpr char TAG_IR[] = "IR";

// Build a Sharp-compatible command frame and transmit it to the heat pump.
// Returns false when the input values are rejected as invalid.
bool IRController::send(uint8_t power, uint8_t tenMode, uint8_t fanSpeed, uint8_t temperature)
{
  ESPLogger.trace(TAG_IR, "IR send request raw values power=%u tenMode=%u fan=%u temp=%u",
                  power, tenMode, fanSpeed, temperature);

  const bool powerIsValid = (power == 0 || power == 1);
  const bool tenModeIsValid = (tenMode == 0 || tenMode == 1);
  const bool fanIsValid = (fanSpeed <= 3);
  const bool tempIsValid = (temperature >= 16 && temperature <= 30);

  uint8_t safePower = powerIsValid ? power : 0;
  uint8_t safeTenMode = tenModeIsValid ? tenMode : 0;
  uint8_t safeFanSpeed = fanIsValid ? fanSpeed : 0;
  uint8_t safeTemperature = tempIsValid ? temperature : 20;

  if (!powerIsValid || !tenModeIsValid || !fanIsValid || !tempIsValid) {
    ESPLogger.error(TAG_IR, "Out-of-range IR command values received (power=%u tenMode=%u fan=%u temp=%u)",
                    power, tenMode, fanSpeed, temperature);
    ESPLogger.warn(TAG_IR, "Invalid IR command values received; using safe defaults (power=%u tenMode=%u fan=%u temp=%u)",
                   safePower, safeTenMode, safeFanSpeed, safeTemperature);
    return false;
  }

  ESPLogger.info(TAG_IR, "Sending IR command power=%u tenMode=%u fan=%u temp=%u",
                 safePower, safeTenMode, safeFanSpeed, safeTemperature);

  //uint8_t SharpTemplate[] = { 0xAA, 0x5A, 0xCF, 0x10, 0x03, 0x21, 0x21, 0x0E, 0x08, 0x80, 0x04, 0xF0, 0xB1 }; // Heat 20 deg auto fan
  //uint8_t SharpTemplate[] = {0xAA, 0x5A, 0xCF, 0x10, 0x03, 0x11, 0x21, 0x0F, 0x08, 0x80, 0x00, 0xF0, 0x01};
  //                             0     1     2     3     4     5     6     7     8     9    10    11    12

  uint8_t SharpTemplate[] = { 0xAA, 0x5A, 0xCF, 0x10, 0x00, 0x11, 0x21, 0x0E, 0x08, 0x80, 0x04, 0xF0, 0x01 };

  if( safePower == 0 ) { // Power off
    SharpTemplate[5] = 0x21; // Power off
    ESPLogger.debug(TAG_IR, "Prepared power-off frame variant");
  } else {
    SharpTemplate[4] = 0x01 + (safeTemperature - 18); // Change temperature

    uint8_t fanSpeeds[] = { 0x21, 0x31, 0x51, 0x71 }; // Auto, low, med, high
    SharpTemplate[6] = fanSpeeds[safeFanSpeed]; // Set fan speed
    ESPLogger.debug(TAG_IR, "Prepared normal frame variant with temp=%u fanIndex=%u", safeTemperature, safeFanSpeed);
  }

  if ( safeTenMode == 1 ) {
    // Send a warm-up command first so the unit accepts the later ten-degree mode frame.
    ESPLogger.info(TAG_IR, "Ten-degree mode requested; sending warm-up frame first");
    send(1,0,3,20);

    // Switch the air conditioner into the special ten-degree mode frame.
    uint8_t TenModeTemplate[] = {0xAA, 0x5A, 0xCF, 0x10, 0x00, 0x31, 0x71, 0x0B, 0x08, 0x80, 0x05, 0xF0, 0x91};
    memcpy(SharpTemplate, TenModeTemplate, sizeof(SharpTemplate));
  } 

  // The protocol uses a simple checksum nibble to validate the frame.
  uint8_t checksum = 0x00;

  // Calculate the checksum over the first 12 bytes and fold it into the final byte.
  for (int i=0; i<12; i++) {
    checksum ^= SharpTemplate[i];
  }

  checksum ^= SharpTemplate[12] & 0x0F;
  checksum ^= (checksum >> 4);
  checksum &= 0x0F;

  SharpTemplate[12] |= (checksum << 4);
  ESPLogger.trace(TAG_IR, "Computed frame checksum nibble=0x%X", checksum);


  // Repeat the transmission a few times so the receiver can pick up the command reliably.
  for(int i = 0; i < 3; i++) {
    ESPLogger.trace(TAG_IR, "Transmitting IR frame repeat %d/3", i + 1);

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

  // Print the final command bytes for debugging and protocol inspection.
  ESPLogger.debug(TAG_IR, "IR frame bytes:");
  for (size_t i = 0; i < sizeof(SharpTemplate); ++i) {
    Serial.print("0x");
    Serial.print(static_cast<int>(SharpTemplate[i]), HEX);
    Serial.print(" ");
  }
  Serial.print("\n");

  ESPLogger.trace(TAG_IR, "IR frame transmission complete");
  return true;
}

