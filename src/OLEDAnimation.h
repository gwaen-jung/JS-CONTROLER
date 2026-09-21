#pragma once

#include <stdint.h>

// So lieu phu hien tren OLED (nhung thu khong can thiet, TFT da lo phan chinh).
struct OledInfo {
  uint32_t sendOk;          // tong so lan gui ESP-NOW thanh cong
  uint32_t sendFail;        // tong so lan gui that bai
  uint16_t throttleUs;      // muc ga dang gui (1000..2000)
  uint32_t telemetryAgeMs;  // ms ke tu goi telemetry cuoi, 0xFFFFFFFF = chua nhan lan nao
  bool armed;
  bool useNrf24;            // true = NRF24, false = ESP-NOW
};

void OLED_Init();
// Chay chu "RESHAPE LAB" chay ngang trong durationMs (chan luon), dung trong setup().
void OLED_BootMarquee(uint32_t durationMs);
// Ve 1 khung cua marquee o thoi diem elapsed/totalMs, de chay chung voi animation khac (vd logo TFT).
void OLED_BootMarqueeFrame(uint32_t elapsed, uint32_t totalMs);
// Sau boot: logo + chu thu nho, roi man hinh thong so. Chay o task rieng, khong chan loop().
void OLED_StartTask();
void OLED_SetInfo(const OledInfo &info);
