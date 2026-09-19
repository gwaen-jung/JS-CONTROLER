#include "display.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <math.h>

static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite pfdSprite = TFT_eSprite(&tft);

// ---- layout (landscape 320x240, setRotation(1)) ----
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;

// ---- top status bar ----
static const int BANNER_H = 20;
static const int NAV_STATUS_Y = 20;
static const int CONN_X   = 4;   // "DRONE: CONNECTED"
static const int MODE_X   = 150; // "[START] / [STOP]"
static const int LINK_X   = 244; // "[ESP NOW] / [NRF]"

// ---- PFD (khung chu nhat chinh giua) ----
static const int PFD_X = 104;
static const int PFD_Y = 30;
static const int PFD_W = 112;
static const int PFD_H = 150;
static const int PFD_CX = PFD_X + PFD_W / 2;
static const int PFD_CY = PFD_Y + PFD_H / 2;

// ---- compass (trai PFD) ----
static const int COMPASS_CX = 52;
static const int COMPASS_CY = 108;
static const int COMPASS_R  = 42;

// ---- vector rotation (phai PFD) ----
static const int VEC_CX = 268;
static const int VEC_CY = 108;
static const int VEC_R  = 42;

// ---- throttle: 4 thanh ngang, bo 2x2 ----
static const int THR_ROW1_Y = 190;
static const int THR_ROW2_Y = 214;
static const int THR_COL1_X = 8;
static const int THR_COL2_X = 166;
static const int THR_LABEL_W = 20;
static const int THR_BAR_W   = 86;
static const int THR_BAR_H   = 16;
static const int THR_PCT_W   = 34;

static bool sInited = false;
static uint16_t sBgColor = TFT_BLACK;
static uint16_t sFgColor = TFT_WHITE;
static DisplayState sMenuState{};
static bool sNeedsDashboardFrame = true;
static bool sPrevArmed = false;
static bool sPrevUseNrf24 = false;
static bool sPrevDroneConnected = false;
static float sPrevLinkQuality = -1.0f;
static uint8_t sPrevGpsFix = 255;
static uint8_t sPrevGpsSatellites = 255;
static uint8_t sPrevNavModeRequested = 255;
static uint8_t sPrevNavMode = 255;
static uint8_t sPrevNavError = 255;
static bool sWasInSettingsMenu = false;
static bool sSettingsInited = false;
static uint8_t sPrevSettingsItem = 255;
static uint8_t sPrevSettingsVolume = 255;
static uint8_t sPrevSettingsBrightness = 255;
static bool sPrevSettingsDarkMode = false;

static const char *navModeText(uint8_t mode) {
  if (mode == 0x02) return "HOLD";
  if (mode == 0x04) return "RTH";
  if (mode == 0x08) return "GEO";
  return "MAN";
}

static void drawNavStatus(const DisplayState &state) {
  if (state.gpsFix == sPrevGpsFix && state.gpsSatellites == sPrevGpsSatellites &&
      state.navModeRequested == sPrevNavModeRequested &&
      state.navMode == sPrevNavMode && state.navError == sPrevNavError) return;
  sPrevGpsFix = state.gpsFix;
  sPrevGpsSatellites = state.gpsSatellites;
  sPrevNavModeRequested = state.navModeRequested;
  sPrevNavMode = state.navMode;
  sPrevNavError = state.navError;
  tft.fillRect(0, NAV_STATUS_Y, SCREEN_W, 10, sBgColor);
  char buf[64];
  const bool pendingMismatch = (state.navModeRequested != state.navMode);
  if (!state.gpsFix) {
    snprintf(buf, sizeof(buf), "GPS:%s SAT:%u REQ:%s ACT:%s",
             state.gpsFix ? "OK" : "--", state.gpsSatellites,
             navModeText(state.navModeRequested), navModeText(state.navMode));
    tft.setTextColor(TFT_RED, sBgColor);
  } else if (state.navError != 0 || pendingMismatch) {
    snprintf(buf, sizeof(buf), "GPS:OK SAT:%u REQ:%s ACT:%s",
             state.gpsSatellites,
             navModeText(state.navModeRequested), navModeText(state.navMode));
    tft.setTextColor(TFT_YELLOW, sBgColor);
  } else {
    snprintf(buf, sizeof(buf), "GPS:%s SAT:%u MODE:%s",
             state.gpsFix ? "OK" : "--", state.gpsSatellites, navModeText(state.navMode));
    tft.setTextColor(TFT_GREEN, sBgColor);
  }
  tft.drawString(buf, 4, NAV_STATUS_Y, 1);
}

// ------------------------------------------------------------------
// Ve khung/nhan CO DINH — chi goi 1 lan trong Display_Init(). Cac ham
// draw*() trong Display_Update() sau nay chi dong cham vao vung DONG
// ben trong nhung khung nay, khong ve lai vien/nhan moi lan.
static void drawStaticFrames() {
  tft.fillScreen(sBgColor);

  // khung PFD
  tft.drawRect(PFD_X, PFD_Y, PFD_W, PFD_H, TFT_DARKGREY);

  // vien compass + vector rotation
  tft.drawCircle(COMPASS_CX, COMPASS_CY, COMPASS_R, TFT_DARKGREY);
  tft.drawCircle(VEC_CX, VEC_CY, VEC_R, TFT_DARKGREY);

  tft.setTextColor(sFgColor, sBgColor);
  tft.setTextSize(1);
  tft.drawString("N", COMPASS_CX - 3, COMPASS_CY - COMPASS_R - 10, 1);
  tft.drawString("S", COMPASS_CX - 3, COMPASS_CY + COMPASS_R + 2, 1);
  tft.drawString("W", COMPASS_CX - COMPASS_R - 12, COMPASS_CY - 4, 1);
  tft.drawString("E", COMPASS_CX + COMPASS_R + 4, COMPASS_CY - 4, 1);

  // nhan M1-M4 canh moi thanh throttle (co dinh, khong doi)
  tft.setTextColor(sFgColor, sBgColor);
  tft.drawString("M1", THR_COL1_X, THR_ROW1_Y + 3, 1);
  tft.drawString("M2", THR_COL2_X, THR_ROW1_Y + 3, 1);
  tft.drawString("M3", THR_COL1_X, THR_ROW2_Y + 3, 1);
  tft.drawString("M4", THR_COL2_X, THR_ROW2_Y + 3, 1);
  tft.drawRect(THR_COL1_X + THR_LABEL_W, THR_ROW1_Y, THR_BAR_W, THR_BAR_H, TFT_DARKGREY);
  tft.drawRect(THR_COL2_X + THR_LABEL_W, THR_ROW1_Y, THR_BAR_W, THR_BAR_H, TFT_DARKGREY);
  tft.drawRect(THR_COL1_X + THR_LABEL_W, THR_ROW2_Y, THR_BAR_W, THR_BAR_H, TFT_DARKGREY);
  tft.drawRect(THR_COL2_X + THR_LABEL_W, THR_ROW2_Y, THR_BAR_W, THR_BAR_H, TFT_DARKGREY);
}

// ------------------------------------------------------------------
// 1. TOP STATUS BAR
//    - DRONE: CONNECTED (xanh) / DISCONNECTED (do)
//    - [START] / [STOP] — cai dang active thi sang mau + khung, cai kia mo
//    - [ESP NOW] / [NRF] — tuong tu
void drawTopBar(bool droneConnected, bool armed, bool useNrf24, float linkQualityPct) {
  bool changed = (droneConnected != sPrevDroneConnected) ||
                 (armed != sPrevArmed) ||
                 (useNrf24 != sPrevUseNrf24) ||
                 (fabsf(linkQualityPct - sPrevLinkQuality) > 0.5f);
  if (!changed) return;
  sPrevDroneConnected = droneConnected;
  sPrevArmed = armed;
  sPrevUseNrf24 = useNrf24;
  sPrevLinkQuality = linkQualityPct;

  tft.fillRect(0, 0, SCREEN_W, BANNER_H, sBgColor);
  tft.setTextSize(1);

  // -- connection indicator --
  uint16_t connColor = droneConnected ? TFT_GREEN : TFT_RED;
  tft.fillCircle(CONN_X + 5, BANNER_H / 2, 5, connColor);
  tft.setTextColor(connColor, sBgColor);
  tft.drawString(droneConnected ? "DRONE: CONNECTED" : "DRONE: DISCONNECTED",
                 CONN_X + 14, 8, 1);

  char linkBuf[16];
  snprintf(linkBuf, sizeof(linkBuf), "LINK %d%%", (int)roundf(linkQualityPct));
  tft.setTextColor(sFgColor, sBgColor);
  tft.drawString(linkBuf, CONN_X + 130, 8, 1);

  // -- [START] / [STOP] --
  uint16_t startColor = armed ? TFT_GREEN : TFT_DARKGREY;
  uint16_t stopColor  = armed ? TFT_DARKGREY : TFT_RED;
  tft.setTextColor(startColor, sBgColor);
  tft.drawString("[START]", MODE_X, 8, 1);
  tft.setTextColor(stopColor, sBgColor);
  tft.drawString("[STOP]", MODE_X + 48, 8, 1);
  if (armed) tft.drawRect(MODE_X - 2, 3, 46, 16, TFT_GREEN);
  else       tft.drawRect(MODE_X + 46, 3, 40, 16, TFT_RED);

  // -- [ESP NOW] / [NRF] --
  uint16_t espColor = useNrf24 ? TFT_DARKGREY : TFT_CYAN;
  uint16_t nrfColor  = useNrf24 ? TFT_CYAN : TFT_DARKGREY;
  tft.setTextColor(espColor, sBgColor);
  tft.drawString("[ESP NOW]", LINK_X, 8, 1);
  tft.setTextColor(nrfColor, sBgColor);
  tft.drawString("[NRF]", LINK_X + 60, 8, 1);
  if (!useNrf24) tft.drawRect(LINK_X - 2, 3, 58, 16, TFT_CYAN);
  else           tft.drawRect(LINK_X + 58, 3, 34, 16, TFT_CYAN);
}

// ------------------------------------------------------------------
// 2. PFD — render to an off-screen sprite, then push once. This keeps the
//    attitude scale consistent and avoids the flicker caused by redrawing the
//    whole widget directly to TFT for every update.
void drawPFD(float rollDeg, float pitchDeg, float altitudeM, float speedKmh, bool fresh) {
  const int innerX = 1;
  const int innerY = 1;
  const int innerW = PFD_W - 2;
  const int innerH = PFD_H - 2;
  const int cx = PFD_W / 2;
  const int cy = PFD_H / 2;

  pfdSprite.fillSprite(sBgColor);
  pfdSprite.setTextColor(sFgColor, sBgColor);
  pfdSprite.setTextSize(1);

  if (!fresh) {
    pfdSprite.drawString("NO LINK", cx - 22, cy - 4, 1);
  } else {
    const float pitchRad = radians(-pitchDeg);
    const float cosR = cosf(pitchRad);
    const float sinR = sinf(pitchRad);

    // Visual swap: roll shifts the horizon up/down; pitch banks the horizon left/right.
    const float horizonBaseY = cy - (rollDeg * PFD_PX_PER_DEG);
    const int x0 = innerX;
    const int x1 = innerX + innerW - 1;
    const int y0 = (int)constrain(horizonBaseY - (x0 - cx) * tanf(pitchRad), innerY, innerY + innerH);
    const int y1 = (int)constrain(horizonBaseY - (x1 - cx) * tanf(pitchRad), innerY, innerY + innerH);

    for (int x = innerX; x <= innerX + innerW; x += 2) {
      const int dx = x - cx;
      const int horizonY = (int)constrain(horizonBaseY - dx * tanf(pitchRad), innerY, innerY + innerH);
      pfdSprite.drawFastVLine(x, innerY, horizonY - innerY, TFT_NAVY);
      pfdSprite.drawFastVLine(x, horizonY, (innerY + innerH) - horizonY, TFT_DARKGREEN);
    }
    pfdSprite.drawLine(x0, y0, x1, y1, sFgColor);

    // Use one authoritative px-per-degree scale throughout the ladder.
    for (int stepDeg = -20; stepDeg <= 20; stepDeg += 5) {
      if (stepDeg == 0) continue;

      const int rungOffsetPx = (int)(stepDeg * PFD_PX_PER_DEG);
      const int yBase = (int)horizonBaseY - rungOffsetPx;
      if (yBase < innerY + 4 || yBase > innerY + innerH - 4) continue;

      const int halfLen = (abs(stepDeg) <= 10) ? 18 : 12;
      const int xLeft = cx - halfLen;
      const int xRight = cx + halfLen;

      const int x1 = cx + (int)((xLeft - cx) * cosR - (yBase - cy) * sinR);
      const int y1 = cy + (int)((xLeft - cx) * sinR + (yBase - cy) * cosR);
      const int x2 = cx + (int)((xRight - cx) * cosR - (yBase - cy) * sinR);
      const int y2 = cy + (int)((xRight - cx) * sinR + (yBase - cy) * cosR);

      pfdSprite.drawLine(x1, y1, x2, y2, sFgColor);

      if (abs(stepDeg) % 10 == 0) {
        const int labelX = cx + 18;
        const int labelY = yBase;
        const int labelXr = cx + (int)((labelX - cx) * cosR - (labelY - cy) * sinR);
        const int labelYr = cy + (int)((labelX - cx) * sinR + (labelY - cy) * cosR);
        if (labelYr >= innerY && labelYr <= innerY + innerH) {
          char label[8];
          snprintf(label, sizeof(label), "%d", abs(stepDeg));
          pfdSprite.drawString(label, labelXr - 4, labelYr - 4, 1);
        }
      }
    }

    pfdSprite.drawFastHLine(cx - 18, cy, 14, TFT_YELLOW);
    pfdSprite.drawFastHLine(cx + 4, cy, 14, TFT_YELLOW);
    pfdSprite.drawFastVLine(cx, cy - 8, 16, TFT_YELLOW);

    const int rollX = cx + (int)(sinf(pitchRad) * 12.0f);
    const int rollY = cy - 22 - (int)(cosf(pitchRad) * 8.0f);
    pfdSprite.fillTriangle(rollX, rollY - 4,
                          rollX - 4, rollY + 4,
                          rollX + 4, rollY + 4,
                          TFT_YELLOW);
  }

  char topBuf[24];
  if (altitudeM < 0 || speedKmh < 0) {
    snprintf(topBuf, sizeof(topBuf), "ALT: --   SPD: --");
  } else {
    snprintf(topBuf, sizeof(topBuf), "ALT:%.0fm SPD:%.0f", altitudeM, speedKmh);
  }
  pfdSprite.fillRect(innerX, innerY, innerW, 10, sBgColor);
  pfdSprite.drawString(topBuf, innerX + 2, innerY, 1);

  char botBuf[24];
  if (fresh) snprintf(botBuf, sizeof(botBuf), "R:%+.0f P:%+.0f", rollDeg, pitchDeg);
  else       snprintf(botBuf, sizeof(botBuf), "R:-- P:--");
  pfdSprite.fillRect(innerX, innerY + innerH - 10, innerW, 10, sBgColor);
  pfdSprite.drawString(botBuf, innerX + 2, innerY + innerH - 10, 1);

  pfdSprite.pushSprite(PFD_X, PFD_Y);
}

// ------------------------------------------------------------------
// 3. COMPASS — the co dinh N/S/E/W (ve tinh trong drawStaticFrames),
//    kim mau cam xoay theo yaw THAT cua drone.
void drawCompass(float yawDeg, bool fresh) {
  tft.fillCircle(COMPASS_CX, COMPASS_CY, COMPASS_R - 1, sBgColor);

  if (!fresh) {
    tft.setTextColor(TFT_RED, sBgColor);
    tft.drawString("NO LINK", COMPASS_CX - 22, COMPASS_CY - 4, 1);
  } else {
    float a = radians(yawDeg);
    int tipX = COMPASS_CX + (int)(sinf(a) * (COMPASS_R - 8));
    int tipY = COMPASS_CY - (int)(cosf(a) * (COMPASS_R - 8));
    int tailX = COMPASS_CX - (int)(sinf(a) * (COMPASS_R * 0.4f));
    int tailY = COMPASS_CY + (int)(cosf(a) * (COMPASS_R * 0.4f));
    tft.drawLine(tailX, tailY, tipX, tipY, TFT_ORANGE);
    tft.fillTriangle(tipX, tipY,
                      tipX - (int)(6 * sinf(a + 2.6f)), tipY + (int)(6 * cosf(a + 2.6f)),
                      tipX - (int)(6 * sinf(a - 2.6f)), tipY + (int)(6 * cosf(a - 2.6f)),
                      TFT_ORANGE);
  }
  tft.fillCircle(COMPASS_CX, COMPASS_CY, 2, sFgColor);

  char buf[20];
  if (fresh) snprintf(buf, sizeof(buf), "Heading: %03.0f", fmodf(yawDeg + 360.0f, 360.0f));
  else       snprintf(buf, sizeof(buf), "Heading: --");
  tft.fillRect(COMPASS_CX - 40, COMPASS_CY + COMPASS_R + 12, 80, 10, sBgColor);
  tft.setTextColor(TFT_YELLOW, sBgColor);
  tft.drawString(buf, COMPASS_CX - 34, COMPASS_CY + COMPASS_R + 12, 1);
}

// ------------------------------------------------------------------
// 4b. VECTOR ROTATION — vector TONG HOP tu roll+pitch (khong phai la ban),
//     de nhin nhanh drone dang nghieng ve huong nao va nghieng bao nhieu.
//     day KHONG phai huong bay that, chi la chi so truc quan.
void drawVectorRotation(float rollDeg, float pitchDeg, bool fresh) {
  tft.fillCircle(VEC_CX, VEC_CY, VEC_R - 1, sBgColor);

  float degOut = 0;
  if (!fresh) {
    tft.setTextColor(TFT_RED, sBgColor);
    tft.drawString("NO LINK", VEC_CX - 22, VEC_CY - 4, 1);
  } else {
    float mag = sqrtf(rollDeg * rollDeg + pitchDeg * pitchDeg);
    degOut = mag;
    float a = atan2f(rollDeg, pitchDeg); // 0 do = huong "len" khi pitch duong
    float scale = constrain(mag, 0.0f, 45.0f) / 45.0f; // 45 do = het ban kinh
    int tipX = VEC_CX + (int)(sinf(a) * (VEC_R - 8) * scale);
    int tipY = VEC_CY - (int)(cosf(a) * (VEC_R - 8) * scale);
    tft.drawLine(VEC_CX, VEC_CY, tipX, tipY, TFT_CYAN);
    tft.fillTriangle(tipX, tipY,
                      tipX - (int)(6 * sinf(a + 2.6f)), tipY + (int)(6 * cosf(a + 2.6f)),
                      tipX - (int)(6 * sinf(a - 2.6f)), tipY + (int)(6 * cosf(a - 2.6f)),
                      TFT_CYAN);
  }
  tft.fillCircle(VEC_CX, VEC_CY, 2, sFgColor);

  char buf[16];
  if (fresh) snprintf(buf, sizeof(buf), "%.0f deg", degOut);
  else       snprintf(buf, sizeof(buf), "--");
  tft.fillRect(VEC_CX - 30, VEC_CY + VEC_R + 12, 60, 10, sBgColor);
  tft.setTextColor(TFT_YELLOW, sBgColor);
  tft.drawString(buf, VEC_CX - 16, VEC_CY + VEC_R + 12, 1);
}

// ------------------------------------------------------------------
// 5. THROTTLE — 4 thanh ngang, bo 2x2, hien % thay vi gia tri DShot tho.
static void drawOneBar(int x, int y, uint16_t dshotValue, bool fresh) {
  int barX = x + THR_LABEL_W;
  tft.fillRect(barX + 1, y + 1, THR_BAR_W - 2, THR_BAR_H - 2, sBgColor);

  char pctBuf[6] = "--";
  if (fresh) {
    int pct = map(constrain((int)dshotValue, 1000, 2000), 1000, 2000, 0, 100);
    int fillW = map(pct, 0, 100, 0, THR_BAR_W - 2);
    uint16_t color = pct < 5 ? TFT_DARKGREY : TFT_CYAN;
    tft.fillRect(barX + 1, y + 1, fillW, THR_BAR_H - 2, color);
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
  }
  tft.fillRect(barX + THR_BAR_W + 2, y + 2, THR_PCT_W, THR_BAR_H - 4, sBgColor);
  tft.setTextColor(sFgColor, sBgColor);
  tft.drawString(pctBuf, barX + THR_BAR_W + 2, y + 4, 1);
}

void drawThrottle(uint16_t m1, uint16_t m2, uint16_t m3, uint16_t m4, bool fresh) {
  drawOneBar(THR_COL1_X, THR_ROW1_Y, m1, fresh);
  drawOneBar(THR_COL2_X, THR_ROW1_Y, m2, fresh);
  drawOneBar(THR_COL1_X, THR_ROW2_Y, m3, fresh);
  drawOneBar(THR_COL2_X, THR_ROW2_Y, m4, fresh);
}

// ------------------------------------------------------------------
// Khung tinh (tieu de, huong dan, footer) chi ve 1 lan khi VUA vao settings.
static void drawSettingsStaticFrame() {
  tft.fillScreen(sBgColor);
  tft.setTextColor(sFgColor, sBgColor);
  tft.setTextSize(2);
  tft.drawString("SETTINGS", 12, 10, 2);
  tft.setTextSize(1);
  tft.drawString("J2 pitch: select   NAV: change", 12, 38, 1);
  tft.setTextColor(TFT_DARKGREY, sBgColor);
  tft.drawString("Move MTS-102 back to return", 12, 218, 1);
}

static void drawSettingsItem(uint8_t i, bool selected) {
  const uint16_t accent = TFT_CYAN;
  static const char *items[] = {"Volume", "Brightness", "Theme"};
  const int y = 70 + i * 42;
  tft.fillRect(8, y - 5, 304, 32, sBgColor);
  if (selected) {
    tft.fillRoundRect(8, y - 5, 304, 32, 4, accent);
    tft.setTextColor(TFT_BLACK, accent);
  } else {
    tft.setTextColor(sFgColor, sBgColor);
  }
  tft.drawString(items[i], 20, y + 3, 2);
  char value[24];
  if (i == 0) snprintf(value, sizeof(value), "%u%%", sMenuState.volume);
  else if (i == 1) snprintf(value, sizeof(value), "%u%%", sMenuState.brightness);
  else snprintf(value, sizeof(value), "%s", sMenuState.darkMode ? "Dark" : "Light");
  tft.drawRightString(value, 292, y + 3, 2);
}

// Chi ve lai toan man hinh khi VUA vao settings (sSettingsInited=false, dat
// boi Display_Update khi phat hien chuyen tu dashboard sang settings). Cac
// lan goi sau chi ve lai dong item nao thay doi lua chon/gia tri, giong
// kieu partial-redraw cua dashboard, tranh fillScreen moi frame gay giat.
void Display_ShowSettingsMenu() {
  if (!sInited) return;
  if (!sSettingsInited) {
    drawSettingsStaticFrame();
    sSettingsInited = true;
    sPrevSettingsItem = 255;
    sPrevSettingsVolume = 255;
    sPrevSettingsBrightness = 255;
    sPrevSettingsDarkMode = !sMenuState.darkMode;
  }

  const bool selectionChanged = sMenuState.settingsItem != sPrevSettingsItem;
  const bool valuesChanged = sMenuState.volume != sPrevSettingsVolume ||
                             sMenuState.brightness != sPrevSettingsBrightness ||
                             sMenuState.darkMode != sPrevSettingsDarkMode;
  if (!selectionChanged && !valuesChanged) return;

  for (uint8_t i = 0; i < 3; ++i) {
    drawSettingsItem(i, i == sMenuState.settingsItem);
  }
  sPrevSettingsItem = sMenuState.settingsItem;
  sPrevSettingsVolume = sMenuState.volume;
  sPrevSettingsBrightness = sMenuState.brightness;
  sPrevSettingsDarkMode = sMenuState.darkMode;
}
void Display_Init() {
  tft.init();
  tft.setRotation(3);
  pfdSprite.createSprite(PFD_W, PFD_H);
  drawStaticFrames();
  sInited = true;
  sPrevArmed = false;
  sPrevUseNrf24 = false;
  sPrevDroneConnected = false;
}

void Display_Update(const DisplayState &state) {
  if (!sInited) return;

  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs < 50) return; // ~20Hz, muot hon, van khong lam nghen loop() RC
  lastMs = now;

  sMenuState = state;
  if (state.inSettingsMenu) {
    sBgColor = state.darkMode ? TFT_BLACK : TFT_WHITE;
    sFgColor = state.darkMode ? TFT_WHITE : TFT_BLACK;
    if (!sWasInSettingsMenu) sSettingsInited = false; // vua vao settings -> ve lai khung tinh
    sWasInSettingsMenu = true;
    sNeedsDashboardFrame = true;
    Display_ShowSettingsMenu();
    return;
  }
  sWasInSettingsMenu = false;
  sBgColor = state.darkMode ? TFT_BLACK : TFT_WHITE;
  sFgColor = state.darkMode ? TFT_WHITE : TFT_BLACK;
  if (state.brightness < 75) sFgColor = state.darkMode ? TFT_LIGHTGREY : TFT_DARKGREY;
  if (sNeedsDashboardFrame) {
    drawStaticFrames();
    sNeedsDashboardFrame = false;
  }

  bool bannerArmed = state.droneLinkFresh ? state.droneArmed : state.armed;

  drawTopBar(state.droneLinkFresh, bannerArmed, state.useNrf24, state.linkQualityPct);
  drawNavStatus(state);
  drawPFD(state.droneRoll, state.dronePitch, state.droneAltitudeM, state.droneSpeedKmh,
          state.droneLinkFresh);
  drawCompass(state.droneYaw, state.droneLinkFresh);
  drawVectorRotation(state.droneRoll, state.dronePitch, state.droneLinkFresh);
  drawThrottle(state.m1, state.m2, state.m3, state.m4, state.droneLinkFresh);
}