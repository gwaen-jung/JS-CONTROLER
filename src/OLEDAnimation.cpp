#include "OLEDAnimation.h"

#if defined(ESP32)
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

namespace {
constexpr uint8_t kOledWidth = 128;
constexpr uint8_t kOledHeight = 64;
constexpr uint8_t kOledAddress = 0x3C;
constexpr int kSdaPin = 21;
constexpr int kSclPin = 22;

Adafruit_SSD1306 oled(kOledWidth, kOledHeight, &Wire, -1);
bool oledReady = false;
uint32_t lastFrameMs = 0;
uint32_t frameNumber = 0;

void drawStarfield() {
  for (uint8_t i = 0; i < 18; ++i) {
    const int x = (static_cast<int>(i) * 37 - static_cast<int>(frameNumber / (i % 3 + 1))) % 140;
    const int y = 12 + (i * 17) % 38;
    const int wrappedX = x < 0 ? x + 140 : x;
    if (wrappedX < 128) {
      oled.drawPixel(wrappedX, y, SSD1306_WHITE);
    }
  }
}

void drawUav(int centerX, int centerY, int tilt, bool propellerPhase) {
  oled.drawLine(centerX - 25, centerY - 8 + tilt, centerX + 25, centerY + 8 + tilt, SSD1306_WHITE);
  oled.drawLine(centerX - 18, centerY - 13 + tilt, centerX + 18, centerY + 13 + tilt, SSD1306_WHITE);
  oled.fillTriangle(centerX - 8, centerY - 3 + tilt,
                    centerX + 8, centerY - 3 + tilt,
                    centerX, centerY + 10 + tilt, SSD1306_WHITE);
  oled.drawLine(centerX - 3, centerY + 5 + tilt, centerX + 3, centerY + 5 + tilt, SSD1306_BLACK);

  const int propellerOffset = propellerPhase ? 3 : -3;
  oled.drawCircle(centerX - 25, centerY - 8 + tilt, 5, SSD1306_WHITE);
  oled.drawCircle(centerX + 25, centerY + 8 + tilt, 5, SSD1306_WHITE);
  oled.drawLine(centerX - 29, centerY - 8 + tilt + propellerOffset,
                centerX - 21, centerY - 8 + tilt - propellerOffset, SSD1306_BLACK);
  oled.drawLine(centerX + 21, centerY + 8 + tilt + propellerOffset,
                centerX + 29, centerY + 8 + tilt - propellerOffset, SSD1306_BLACK);
}
}

void OLED_Init() {
  Wire.begin(kSdaPin, kSclPin);
  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, kOledAddress);
  if (!oledReady) {
    Serial.println("[OLED] SSD1306 not found at 0x3C (module label 0x78)");
    return;
  }

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(34, 27);
  oled.print("UAV ONLINE");
  oled.display();
  Serial.println("[OLED] SSD1306 ready SDA=21 SCL=22 addr=0x3C");
}

void OLED_Update() {
  if (!oledReady || millis() - lastFrameMs < 50) return;
  lastFrameMs = millis();
  ++frameNumber;

  const float phase = frameNumber * 0.08f;
  const int centerX = 64 + static_cast<int>(sinf(phase * 0.7f) * 18.0f);
  const int centerY = 37 + static_cast<int>(sinf(phase) * 7.0f);
  const int tilt = static_cast<int>(sinf(phase * 0.7f) * 3.0f);

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(2, 1);
  oled.print("UAV FLIGHT");
  oled.setCursor(91, 1);
  oled.print(frameNumber % 2 ? "*" : " ");
  drawStarfield();
  drawUav(centerX, centerY, tilt, (frameNumber / 3) % 2 != 0);
  oled.drawLine(0, 58, 127, 58, SSD1306_WHITE);
  oled.setCursor(4, 59);
  oled.print("ALT  ACTIVE");
  oled.display();
}

#else
void OLED_Init() {}
void OLED_Update() {}
#endif
