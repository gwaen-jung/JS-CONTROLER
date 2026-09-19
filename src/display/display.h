#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// =========================================================================
// Module Display v3 — man hinh TFT SPI 2.8" 320x240 (ILI9341, v1.2) tren
// tay cam, landscape. Cau truc theo dung yeu cau: 4 ham ve rieng biet
// drawTopBar() / drawPFD() / drawCompass() / drawThrottle(), cong them
// drawVectorRotation() (tach rieng khoi compass vi 2 vong tron nay hien
// thi 2 loai du lieu khac nhau — heading THAT vs vector nghieng tong hop
// roll+pitch — gop chung 1 ham se kho doc va kho cap nhat rieng le).
//
// Kien truc: phan TINH (khung, vien, nhan chu co dinh) chi ve 1 lan trong
// Display_Init(). Moi ham draw*() khi goi trong Display_Update() chi xoa
// va ve lai VUNG DONG cua rieng no (khong fillScreen toan man hinh), giu
// dung tinh than "primitive graphics, tranh ngon RAM" da yeu cau.
//
// CAN THIET DE CO DU LIEU THAT: mo rong 1 chieu ESP-NOW nguoc
// (Drone -> Controller) — xem TelemetryPacket trong RF_Protocol.h va file
// brief COPILOT_BRIEF_TFT_TELEMETRY.md di kem.
// =========================================================================

static const float PFD_PX_PER_DEG = 4.2f;

void Display_Init();

struct DisplayState {
  bool inSettingsMenu;
  uint8_t settingsItem;
  uint8_t volume;
  uint8_t brightness;
  bool darkMode;
  // --- trang thai dieu khien cuc bo (tay cam tu biet, khong can telemetry) ---
  bool armed;      // dang o che do bay (START) hay dung (STOP) — trang thai GUI DI
  bool useNrf24;   // true = dang dung NRF24, false = dang dung ESP-NOW

  // --- telemetry THAT tu drone, qua TelemetryPacket (MOI) ---
  bool  droneLinkFresh;  // co nhan duoc goi telemetry moi trong ~500ms gan day khong
  bool  droneArmed;      // trang thai arm THAT tren drone (uu tien hien thi cai nay)
  float linkQualityPct;  // 0..100, tinh tu success/fail count cua ESP-NOW
  float droneRoll;       // do
  float dronePitch;      // do
  float droneYaw;        // do, dung lam heading la ban (0 = Bac)
  uint16_t m1, m2, m3, m4; // gia tri DShot that (1000..2000), doi ra % khi ve

  // --- ALT/SPD: CHUA CO NGUON DU LIEU THAT (chua co GPS/baro tren drone) ---
  // De -1 = "khong co du lieu", man hinh se hien "--" thay vi so gia.
  // Dien so that ngay khi drone co cam bien do cao/toc do.
  float droneAltitudeM;  // met, -1 = khong co
  float droneSpeedKmh;   // km/h, -1 = khong co
  uint8_t gpsFix;
  uint8_t gpsSatellites;
  uint8_t navModeRequested;
  uint8_t navMode;
  uint8_t navError;
};

// Goi trong loop(), co the goi moi vong lap — ham tu rate-limit noi bo (~10Hz).
void Display_Update(const DisplayState &state);
void Display_ShowSettingsMenu();

#endif