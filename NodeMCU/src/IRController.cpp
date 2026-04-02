#include "Logger.h"

#include <Arduino.h>
#include <string.h>
#include "IRController.h"


// === IR Protocol Constants ===
#define IR_SEND_PIN 5
#define IR_SEND_REPEAT 15
#define IR_SEND_DELAY_MS 200
#define IR_TEN_MODE_START_DELAY_MS 10000

// Sharp protocol header and bit timing (replace with actual values if needed)
#define SHARP_AIRCON1_HDR_MARK 3400
#define SHARP_AIRCON1_HDR_SPACE 1750
#define SHARP_AIRCON1_BIT_MARK 400
#define SHARP_AIRCON1_ONE_SPACE 1300
#define SHARP_AIRCON1_ZERO_SPACE 400

// Fan speed codes (auto, low, med, high)
const uint8_t FAN_SPEED_CODES[] = { 0x21, 0x31, 0x51, 0x71 };

IRSenderESP8266 IR(IR_SEND_PIN);

namespace {
// Helper: Calculate checksum for SharpTemplate
uint8_t calculateChecksum(uint8_t* data, size_t len) {
  uint8_t checksum = 0x00;
  for (size_t i = 0; i < len - 1; i++) {
    checksum ^= data[i];
  }
  checksum ^= data[len - 1] & 0x0F;
  checksum ^= (checksum >> 4);
  checksum &= 0x0F;
  return checksum;
}

// Helper: Send IR command array multiple times
void sendIRCommand(uint8_t* data, size_t len) {
  for (int i = 0; i < IR_SEND_REPEAT; i++) {
    IR.setFrequency(38);
    IR.mark(SHARP_AIRCON1_HDR_MARK);
    IR.space(SHARP_AIRCON1_HDR_SPACE);
    for (size_t j = 0; j < len; j++) {
      IR.sendIRbyte(data[j], SHARP_AIRCON1_BIT_MARK, SHARP_AIRCON1_ZERO_SPACE, SHARP_AIRCON1_ONE_SPACE);
    }
    IR.mark(SHARP_AIRCON1_BIT_MARK);
    IR.space(0);
    delay(IR_SEND_DELAY_MS);
  }
}

// Helper: Print IR command for debugging
void printIRCommand(uint8_t* data, size_t len) {
   // Print IR command for debugging
  String irHex;
  for (size_t i = 0; i < len; ++i) {
    char hexbuf[8];
    snprintf(hexbuf, sizeof(hexbuf), "0x%02X ", data[i]);
    irHex += hexbuf;
  }
  Logger::log(Logger::LOG_DEBUG, String("IR command bytes: ") + irHex, "IRController");
}


// Helper: Prepare ten-degree mode template
void prepareTenDegreeTemplate(uint8_t* dest, size_t len) {
  uint8_t TenModeTemplate[] = {0xAA, 0x5A, 0xCF, 0x10, 0x00, 0x31, 0x71, 0x0B, 0x08, 0x80, 0x05, 0xF0, 0x91};
  memcpy(dest, TenModeTemplate, len);
}
}



namespace IRController {
void send(uint8_t power, uint8_t tenMode, uint8_t fanSpeed, uint8_t temperature)
{
  // Default IR command template for Sharp heatpump
  uint8_t SharpTemplate[] = { 0xAA, 0x5A, 0xCF, 0x10, 0x00, 0x11, 0x21, 0x0E, 0x08, 0x80, 0x04, 0xF0, 0x01 };

  // Input validation
  if (fanSpeed > 3) fanSpeed = 0; // Default to auto if out of range
  if (temperature < 18) temperature = 18;
  if (temperature > 30) temperature = 30;

  if (tenMode == 1) {
    // Start heatpump at 20°C, high fan, power on
    send(1, 0, 3, 18);
    delay(IR_TEN_MODE_START_DELAY_MS);
    prepareTenDegreeTemplate(SharpTemplate, sizeof(SharpTemplate));
  } else if (power == 0) {
    SharpTemplate[5] = 0x21; // Power off code
  } else {
    SharpTemplate[4] = 0x01 + (temperature - 18); // Temperature offset (18°C base)
    SharpTemplate[6] = FAN_SPEED_CODES[fanSpeed]; // Set fan speed
  }

  // Calculate and set checksum
  uint8_t checksum = calculateChecksum(SharpTemplate, sizeof(SharpTemplate));
  SharpTemplate[12] |= (checksum << 4);

  Logger::log(Logger::LOG_DEBUG, "Sending IR command", "IRController");
  sendIRCommand(SharpTemplate, sizeof(SharpTemplate));
  printIRCommand(SharpTemplate, sizeof(SharpTemplate));
}
} // namespace IRController

