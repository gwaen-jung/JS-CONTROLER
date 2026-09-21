#include "OLEDAnimation.h"

#if defined(ESP32)
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "Logo/Logo.h"

namespace {
constexpr uint8_t kOledWidth = 128;
constexpr uint8_t kOledHeight = 64;
constexpr uint8_t kOledAddress = 0x3C;
constexpr int kSdaPin = 21;
constexpr int kSclPin = 22;
constexpr char kBrand[] = "RESHAPE LAB";

Adafruit_SSD1306 oled(kOledWidth, kOledHeight, &Wire, -1);
bool oledReady = false;
TaskHandle_t oledTask = nullptr;
// Ghi tu loop(), doc tu task OLED. Doc lech nhau chi lam sai so 1 khung hinh nen khong can khoa.
OledInfo gInfo{};

// Ve chu thu phong (ti le khong nguyen) quanh tam (centerX, centerY): render chu size 1 vao
// canvas nho roi phong to tung diem anh, de chu co the "to -> nho" muot thay vi nhay theo size 1,2,3.
void drawScaledText(const char *text, int centerX, int centerY, float scale) {
  const int srcW = static_cast<int>(strlen(text)) * 6;
  GFXcanvas1 canvas(srcW, 8);
  canvas.setTextSize(1);
  canvas.setTextColor(1);
  canvas.setCursor(0, 0);
  canvas.print(text);

  const int dstW = lroundf(srcW * scale);
  const int dstH = lroundf(8 * scale);
  const int left = centerX - dstW / 2;
  const int top = centerY - dstH / 2;
  for (int sy = 0; sy < 8; ++sy) {
    const int y0 = top + lroundf(sy * scale);
    const int y1 = top + lroundf((sy + 1) * scale);
    for (int sx = 0; sx < srcW; ++sx) {
      if (!canvas.getPixel(sx, sy)) continue;
      const int x0 = left + lroundf(sx * scale);
      const int x1 = left + lroundf((sx + 1) * scale);
      oled.fillRect(x0, y0, x1 - x0, y1 - y0, SSD1306_WHITE);
    }
  }
}

// Logo hien dan tu tren xuong, sau do chu RESHAPE LAB thu nho dan ngay duoi logo.
void runIntro() {
  constexpr uint32_t kRevealMs = 350;
  constexpr uint32_t kShrinkMs = 1000;
  constexpr uint32_t kHoldMs = 500;
  constexpr int kLogoX = (kOledWidth - kLogoOledBigW) / 2;

  const uint32_t start = millis();
  for (;;) {
    const uint32_t elapsed = millis() - start;
    if (elapsed >= kRevealMs + kShrinkMs + kHoldMs) break;

    oled.clearDisplay();
    oled.drawBitmap(kLogoX, 0, kLogoOledBig, kLogoOledBigW, kLogoOledBigH, SSD1306_WHITE);
    if (elapsed < kRevealMs) {
      const int shown = static_cast<int>((elapsed * kLogoOledBigH) / kRevealMs);
      oled.fillRect(kLogoX, shown, kLogoOledBigW, kLogoOledBigH - shown, SSD1306_BLACK);
    } else {
      const float t = min(1.0f, (elapsed - kRevealMs) / static_cast<float>(kShrinkMs));
      const float inv = 1.0f - t;
      drawScaledText(kBrand, kOledWidth / 2, 52, 1.0f + 0.9f * inv * inv);
    }
    oled.display();
    vTaskDelay(1);
  }
}

void drawInfoScreen(uint16_t rateHz) {
  const OledInfo info = gInfo;
  char buf[32];

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  oled.drawBitmap(0, 0, kLogoOledSmall, kLogoOledSmallW, kLogoOledSmallH, SSD1306_WHITE);
  oled.setCursor(32, 0);
  oled.print(kBrand);
  oled.setCursor(32, 8);
  oled.print("TX CONTROLLER");
  const uint32_t s = millis() / 1000;
  snprintf(buf, sizeof(buf), "UP %02lu:%02lu:%02lu",
           static_cast<unsigned long>(s / 3600), static_cast<unsigned long>((s / 60) % 60),
           static_cast<unsigned long>(s % 60));
  oled.setCursor(32, 21);
  oled.print(buf);
  oled.drawFastHLine(0, 31, kOledWidth, SSD1306_WHITE);

  const unsigned long fail = info.sendFail > 9999 ? 9999 : info.sendFail;
  if (info.useNrf24) {
    snprintf(buf, sizeof(buf), "RC  NRF24");
  } else {
    snprintf(buf, sizeof(buf), "RC %3uHz  FAIL %lu%s", rateHz, fail, info.sendFail > 9999 ? "+" : "");
  }
  oled.setCursor(0, 35);
  oled.print(buf);

  snprintf(buf, sizeof(buf), "THR %4uus  %s", info.throttleUs, info.armed ? "ARMED" : "SAFE");
  oled.setCursor(0, 44);
  oled.print(buf);

  const unsigned long heapK = ESP.getFreeHeap() / 1024;
  if (info.telemetryAgeMs == 0xFFFFFFFFu) {
    snprintf(buf, sizeof(buf), "TEL  --   HEAP %luK", heapK);
  } else {
    const unsigned long age = info.telemetryAgeMs > 9999 ? 9999 : info.telemetryAgeMs;
    snprintf(buf, sizeof(buf), "TEL %4lums HEAP %luK", age, heapK);
  }
  oled.setCursor(0, 53);
  oled.print(buf);

  oled.display();
}

void oledTaskFn(void *) {
  runIntro();

  uint32_t lastRateMs = millis();
  uint32_t lastTotal = gInfo.sendOk + gInfo.sendFail;
  uint16_t rateHz = 0;
  for (;;) {
    const uint32_t now = millis();
    if (now - lastRateMs >= 1000) {
      const uint32_t total = gInfo.sendOk + gInfo.sendFail;
      rateHz = static_cast<uint16_t>((total - lastTotal) * 1000UL / (now - lastRateMs));
      lastTotal = total;
      lastRateMs = now;
    }
    drawInfoScreen(rateHz);
    vTaskDelay(pdMS_TO_TICKS(150));
  }
}
}  // namespace

void OLED_Init() {
  Wire.begin(kSdaPin, kSclPin);
  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, kOledAddress);
  if (!oledReady) {
    Serial.println("[OLED] SSD1306 not found at 0x3C (module label 0x78)");
    return;
  }

  oled.clearDisplay();
  oled.display();
  Serial.println("[OLED] SSD1306 ready SDA=21 SCL=22 addr=0x3C");
}

void OLED_BootMarqueeFrame(uint32_t elapsed, uint32_t totalMs) {
  if (!oledReady || totalMs == 0) return;

  constexpr int kTextW = 11 * 12;  // "RESHAPE LAB" o size 2
  if (elapsed > totalMs) elapsed = totalMs;
  oled.setTextSize(2);
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);
  const int x = kOledWidth - static_cast<int>((static_cast<uint64_t>(kOledWidth + kTextW) * elapsed) / totalMs);
  oled.clearDisplay();
  oled.setCursor(x, 24);
  oled.print(kBrand);
  oled.display();
  oled.setTextWrap(true);
}

void OLED_BootMarquee(uint32_t durationMs) {
  if (!oledReady) {
    delay(durationMs);
    return;
  }

  const uint32_t start = millis();
  for (;;) {
    const uint32_t elapsed = millis() - start;
    if (elapsed >= durationMs) break;
    OLED_BootMarqueeFrame(elapsed, durationMs);
  }
}

void OLED_StartTask() {
  if (!oledReady || oledTask != nullptr) return;
  xTaskCreatePinnedToCore(oledTaskFn, "oled", 4096, nullptr, 1, &oledTask, 0);
}

void OLED_SetInfo(const OledInfo &info) {
  gInfo = info;
}

#else
void OLED_Init() {}
void OLED_BootMarquee(uint32_t) {}
void OLED_BootMarqueeFrame(uint32_t, uint32_t) {}
void OLED_StartTask() {}
void OLED_SetInfo(const OledInfo &) {}
#endif
