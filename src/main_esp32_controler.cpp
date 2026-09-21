#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>
#include "esp_wifi.h"
#include "display/display.h"
#include "RF_Protocol.h"
#include "RF_TX/RF_TX.h"
#include "OLEDAnimation.h"
#include "Audio/Audio.h"

#define CTRL_MODE_PIN 25  // SW (button) from Joystick1 -> use a digital pin (GPIO25) with internal pull-up
#define NAV_MODE_PIN 13    // J2 SW moved to D2 (GPIO2) on ESP32
#define CTRL_MODE_HOLD_MS 5000
#define NAV_MODE_HOLD_MS 100
#define MTS102_SW_PIN 36

// Web/AP is disabled in the minimal flight path, so ESP-NOW receives on the STA MAC.
static const uint8_t kDroneMac[6] = {0x08, 0xD1, 0xF9, 0x29, 0xEA, 0x6C};
static bool modeEspNow = true;
static bool holdDetected = false;
static uint32_t holdStartMs = 0;
static bool espNowReady = false;
static bool nrfReady = false;
static RF24 localRadio(RF_CE_PIN, RF_CSN_PIN);

static bool espNowSendPending = false; // true = Ä‘ang chá» callback cá»§a láº§n gá»­i trÆ°á»›c
static bool sendingEnabled = false;
static bool disarmPending = false;
static uint32_t disarmPendingUntilMs = 0;
static bool gLastSendOk = false;
static uint32_t gSendOkCount = 0;
static uint32_t gSendFailCount = 0;
static uint32_t gConsecutiveSendFailCount = 0;
static bool gSendErrorAudioLatched = false;
static TelemetryPacket gTelemetry{};
static uint32_t gTelemetryLastMs = 0;
static uint8_t navMode = 0;
static bool navSwitchPrev = true;
static bool navHoldDetected = false;
static uint32_t navHoldStartMs = 0;
static Preferences controllerPrefs;
static bool inSettingsMenu = false;
static uint8_t settingsItem = 0;
static uint8_t settingsVolume = 20;
static uint8_t settingsBrightness = 100;
static bool settingsDarkMode = true;
static bool menuPitchLatched = false;
static bool menuNavPrev = false;
static String simTelBuf;
static const uint32_t SIM_TEL_BUF_MAX = 128;

static uint32_t gLastVoiceNotificationMs = 0;

static void playVoiceNotification(VoiceClip clip) {
  const uint32_t now = millis();
  if (gLastVoiceNotificationMs != 0 && now - gLastVoiceNotificationMs < 5000) return;
  Audio_PlayVoice(clip);
  gLastVoiceNotificationMs = now;
}
// Trong luc boot (logo TFT + OLED chay ~2.5s) loop() chua chay nen Audio_Update() khong duoc goi deu,
// bo dem I2S (~100ms) can -> tieng bi giat. Task nay bom audio doc lap voi toc do ve man hinh.
static volatile bool gAudioPumpRun = false;
static volatile bool gAudioPumpDone = false;

static void audioPumpTask(void *) {
  while (gAudioPumpRun) {
    Audio_Update();
    vTaskDelay(1);
  }
  gAudioPumpDone = true;
  vTaskDelete(nullptr);
}

static void startAudioPump() {
  gAudioPumpDone = false;
  gAudioPumpRun = true;
  xTaskCreatePinnedToCore(audioPumpTask, "audio_pump", 4096, nullptr, 2, nullptr, 0);
}

// Phai dung han truoc khi loop() chay: Audio_Update() khong an toan neu 2 task cung goi.
static void stopAudioPump() {
  gAudioPumpRun = false;
  while (!gAudioPumpDone) delay(1);
}

static void saveSettings() {
  controllerPrefs.putUChar("volume", settingsVolume);
  controllerPrefs.putUChar("brightness", settingsBrightness);
  controllerPrefs.putBool("dark", settingsDarkMode);
}

static void adjustSelectedSetting() {
  if (settingsItem == 0) {
    settingsVolume = settingsVolume >= 100 ? 0 : settingsVolume + 10;
    Audio_SetVolume(settingsVolume);
  } else if (settingsItem == 1) {
    settingsBrightness = settingsBrightness >= 100 ? 25 : settingsBrightness + 25;
  } else {
    settingsDarkMode = !settingsDarkMode;
  }
  saveSettings();
}
static void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status) {
  espNowSendPending = false; // láº§n gá»­i trÆ°á»›c Ä‘Ã£ hoÃ n táº¥t (dÃ¹ OK hay FAIL) - má»Ÿ khoÃ¡ cho láº§n gá»­i tiáº¿p theo
  gLastSendOk = status == ESP_NOW_SEND_SUCCESS;
  if (gLastSendOk) {
    ++gSendOkCount;
    gConsecutiveSendFailCount = 0;
    gSendErrorAudioLatched = false;
  } else {
    ++gSendFailCount;
    ++gConsecutiveSendFailCount;
  }
  if (status == ESP_NOW_SEND_SUCCESS && disarmPending) {
    disarmPending = false;
    Serial.println("[CTRL] END disarm delivered");
  }
  static uint32_t lastSendLogMs = 0;
  if (millis() - lastSendLogMs >= 500 || status != ESP_NOW_SEND_SUCCESS) {
    lastSendLogMs = millis();
    Serial.printf("[ESP_NOW SEND] status=%s\n",
                  status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
  }
}

static void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  (void)mac;
  if (data == nullptr || len != static_cast<int>(sizeof(TelemetryPacket))) return;

  TelemetryPacket packet;
  memcpy(&packet, data, sizeof(packet));
  if (packet.header != RF_TELEMETRY_HEADER) return;
  if (computeTelemetryChecksum(packet) != packet.checksum) return;

  gTelemetry = packet;
  gTelemetryLastMs = millis();
}

static void applySimTelLine(const String &line) {
  float r, p, y, alt, spd;
  int m1, m2, m3, m4, armed;
  const int n = sscanf(line.c_str() + 8, "%f,%f,%f,%d,%d,%d,%d,%d,%f,%f",
                       &r, &p, &y, &m1, &m2, &m3, &m4, &armed, &alt, &spd);
  if (n != 10) return;

  gTelemetry.header = RF_TELEMETRY_HEADER;
  gTelemetry.roll = static_cast<int16_t>(r * 10.0f);
  gTelemetry.pitch = static_cast<int16_t>(p * 10.0f);
  gTelemetry.yaw = static_cast<int16_t>(y * 10.0f);
  gTelemetry.m1 = static_cast<uint16_t>(m1);
  gTelemetry.m2 = static_cast<uint16_t>(m2);
  gTelemetry.m3 = static_cast<uint16_t>(m3);
  gTelemetry.m4 = static_cast<uint16_t>(m4);
  gTelemetry.armed = static_cast<uint8_t>(armed != 0);
  gTelemetry.gpsFix = 1;
  gTelemetry.gpsAltitudeM = alt;
  gTelemetry.gpsSpeedKmh = spd * 3.6f;
  gTelemetry.gpsSatellites = 0;
  gTelemetry.navMode = 0;
  gTelemetry.navError = 0;
  gTelemetryLastMs = millis();
}

static bool throttleInvert = false;
static bool yawInvert = false;
static int rollCenter = 2048;
static int pitchCenter = 2048;
static int yawCenter = 2048;
static int throttleCenter = 2048; // tÃ¢m nghá»‰ (spring-return) cá»§a J1, hiá»‡u chá»‰nh lÃºc boot

// --- Throttle hold (rate-based, giá»‘ng throttle "helicopter mode") ---
// J1 la joystick tu-hoi-tam: lech khoi tam => tang/giam throttle theo thoi gian,
// tha ve tam => giu nguyen muc throttle hien tai. Khong con map thang vi tri J1
// sang throttle output nua (chi dung vi tri J1 tho de nhan dien cu ARM/DISARM).
static const int THROTTLE_ARM_BASE = 1350;   // muc throttle khoi tao ngay khi vua ARM
static const int THROTTLE_DEADZONE = 90;     // vung chet quanh tam J1 (thang -1000..1000), > deadzone chung 72 mot chut cho an toan
static const float THROTTLE_RATE_PER_SEC = 500.0f; // us/giay toc do doi toi da khi day J1 het hanh trinh (~2s de quet het 1000..2000)
static int gThrottleDeflection = 0;          // -1000..1000, cap nhat moi vong lap trong buildCommand()

static int mapCentered(int raw, int center, bool invert) {
  int32_t value;
  if (raw >= center) {
    value = (static_cast<int32_t>(raw - center) * 1000) / max(1, 4095 - center);
  } else {
    value = (static_cast<int32_t>(raw - center) * 1000) / max(1, center);
  }
  value = constrain(value, -1000L, 1000L);
  return invert ? -value : value;
}

static void initEspNowController() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  // Fix: make sure the controller is on the same WiFi channel as the drone AP/softAP
  // Drone's softAP channel is fixed to 1 in the drone firmware; set controller channel to 1 too.
  esp_err_t ch = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  if (ch == ESP_OK) {
    Serial.println("[CTRL] wifi channel set to 1");
  } else {
    Serial.printf("[CTRL] failed to set wifi channel: %d\n", ch);
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("[CTRL] ESPNow init failed");
    espNowReady = false;
    return;
  }

  esp_now_register_send_cb(onEspNowSent);
  esp_now_register_recv_cb(onEspNowRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, kDroneMac, 6);
  // must match AP channel on drone
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[CTRL] esp_now_add_peer failed");
  }

  espNowReady = true;
  Serial.println("[CTRL] mode = ESPNow");

  // Quick auto-detect for joystick orientation (avoid throttle/yaw stuck at extremes)
  const int SAMPLES = 40;
  long sumThrottle = 0;
  long sumRoll = 0;
  long sumPitch = 0;
  long sumYaw = 0;
  for (int i = 0; i < SAMPLES; ++i) {
    sumThrottle += analogRead(34);
    sumRoll += analogRead(32);
    sumPitch += analogRead(33);
    sumYaw += analogRead(35);
    delay(5);
  }
  int avgThrottle = (int)(sumThrottle / SAMPLES);
  rollCenter = (int)(sumRoll / SAMPLES);
  pitchCenter = (int)(sumPitch / SAMPLES);
  yawCenter = (int)(sumYaw / SAMPLES);
  throttleCenter = avgThrottle; // J1 la stick tu-hoi-tam, tam nghi = vi tri hieu chinh nay
  int avgYaw = (int)(sumYaw / SAMPLES);

  // Heuristic: if throttle rests near high end, invert mapping
  if (avgThrottle > 3000) throttleInvert = true; else throttleInvert = false;
  // For yaw, if resting mapped value far from center, invert mapping
  yawInvert = false;

  Serial.printf("[CAL] throttle=%d rollCenter=%d pitchCenter=%d yawCenter=%d throttleInvert=%d yawInvert=%d\n",
                avgThrottle, rollCenter, pitchCenter, yawCenter, throttleInvert, yawInvert);
}

static void initNrfController() {
  if (!localRadio.begin()) {
    Serial.println("[CTRL] NRF24 init failed");
    nrfReady = false;
    return;
  }

  localRadio.setChannel(76);
  localRadio.setPALevel(RF24_PA_LOW);
  localRadio.setDataRate(RF24_1MBPS);
  localRadio.setPayloadSize(RF_PAYLOAD_SIZE);
  localRadio.setAutoAck(true);
  localRadio.setRetries(5, 15);
  localRadio.openWritingPipe(RF_PIPE_ADDRESS);
  localRadio.stopListening();

  nrfReady = true;
  Serial.println("[CTRL] mode = NRF24");
}

static void applyMode(bool useEspNow) {
  if (useEspNow) {
    if (espNowReady) {
      esp_now_deinit();
    }
    nrfReady = false;
    initEspNowController();
    modeEspNow = true;
    return;
  }

  if (espNowReady) {
    esp_now_deinit();
    espNowReady = false;
  }
  initNrfController();
  modeEspNow = false;
}

static int mapInverted(int raw, bool invert, int outMin, int outMax) {
  if (invert) raw = 4095 - raw;
  return map(raw, 0, 4095, outMin, outMax);
}

static RCCommand buildCommand() {
  RCCommand cmd{};
  cmd.header = RF_PACKET_HEADER;
  int rawRoll = analogRead(32);
  int rawPitch = analogRead(33);
  int rawYaw = analogRead(35);
  int rawThrottle = analogRead(34);

  // Joystick 2 remains visible/transmitted for diagnostics; STM32 ignores it
  // while the bench-test flight path is throttle-only.
  cmd.roll = (int16_t)mapCentered(rawRoll, rollCenter, true); // VRx (GPIO32) lap nguoc, dao dau cho dung chieu
  cmd.pitch = (int16_t)mapCentered(rawPitch, pitchCenter, true); // VRy (GPIO33) dao dau cho dung chieu
  cmd.yaw = (int16_t)mapCentered(rawYaw, yawCenter, yawInvert);
  // cmd.throttle o day van la vi tri VAT LY tho cua J1 (1000..2000), CHI dung de
  // nhan dien cu ARM/DISARM (yeu cau keo throttle xuong day + yaw). Muc throttle
  // THUC gui cho drone khi da armed se duoc ghi de boi logic throttle-hold trong loop().
  cmd.throttle = (int16_t)mapInverted(rawThrottle, throttleInvert, 1000, 2000);
  // Do lech cua J1 so voi tam nghi (spring-return), dung cho throttle-hold ben loop()
  gThrottleDeflection = mapCentered(rawThrottle, throttleCenter, throttleInvert);

  if (abs(cmd.roll) <= 72) cmd.roll = 0;
  if (abs(cmd.pitch) <= 72) cmd.pitch = 0;
  if (abs(cmd.yaw) <= 72) cmd.yaw = 0;

  cmd.aux = 0;
  cmd.checksum = computeChecksum(cmd);
  return cmd;
}

static const int START_THROTTLE = 1000;
static const int START_YAW = 1000;
static const int END_THROTTLE = 1000;
static const int END_YAW = -1000;
static const int TOLERANCE = 80; // +- tolerance for stick detection (increased for robustness)
static const int YAW_ENDPOINT_THRESHOLD = 750; // joystick thuc te thuong khong cham dung +/-1000
static const int START_HOLD_MS = 500; // require holding START combo this long
static const int END_HOLD_MS = 500;   // require holding END combo this long

static void sendControlWithCmd(const RCCommand &cmd) {
  // Rate-limited serial debug for joystick values (once every 200ms)
  static uint32_t lastSerialMs = 0;
  uint32_t now = millis();
  if (now - lastSerialMs >= 200) {
    lastSerialMs = now;
    Serial.printf("JOY1: throttle=%d yaw=%d  JOY2: roll=%d pitch=%d\n", cmd.throttle, cmd.yaw, cmd.roll, cmd.pitch);
  }

  if (!sendingEnabled && !disarmPending) {
    // Stay silent before START; END sends one explicit disarm packet.
    return;
  }

  if (modeEspNow) {
    if (!espNowReady) {
      return;
    }
    // Chá» láº§n gá»­i trÆ°á»›c hoÃ n táº¥t (callback Ä‘Ã£ cháº¡y) trÆ°á»›c khi gá»­i tiáº¿p -
    // Ä‘Ã¢y chÃ­nh lÃ  nguyÃªn nhÃ¢n gÃ¢y lá»—i NO_MEM (12391): gá»i esp_now_send()
    // dá»“n dáº­p trÆ°á»›c khi hÃ ng Ä‘á»£i TX ná»™i bá»™ Ä‘Æ°á»£c giáº£i phÃ³ng sáº½ lÃ m Ä‘áº§y
    // buffer, theo Ä‘Ãºng khuyáº¿n nghá»‹ chÃ­nh thá»©c tá»« tÃ i liá»‡u ESP-IDF.
    if (espNowSendPending) {
      static uint32_t skipCount = 0;
      skipCount++;
      if (skipCount % 50 == 0) { // chá»‰ in nháº¯c nhá»Ÿ má»—i 50 láº§n bá» qua, trÃ¡nh ngáº­p Serial
        Serial.println("[ESP_NOW] bo qua 1 lan gui - dang cho callback lan truoc");
      }
      return;
    }
    espNowSendPending = true;
    esp_err_t res = esp_now_send(kDroneMac, reinterpret_cast<const uint8_t *>(&cmd), sizeof(cmd));
    if (res != ESP_OK) {
      espNowSendPending = false; // gá»i tháº¥t báº¡i ngay láº­p tá»©c thÃ¬ khÃ´ng cÃ³ callback nÃ o sáº½ tá»›i - pháº£i tá»± má»Ÿ khoÃ¡ láº¡i
      Serial.printf("[ESP_NOW] esp_now_send() failed: %d (heap tu do: %u byte)\n", res, ESP.getFreeHeap());
    }
    return;
  }

  if (!nrfReady) {
    return;
  }
  localRadio.write(&cmd, sizeof(cmd));
}

void setup() {
  Serial.begin(115200);
  delay(500);

  bool audioOk = Audio_Init();
  Serial.printf("[CTRL] Audio_Init() = %s\n", audioOk ? "OK" : "FAIL");
  Audio_SetVolume(20);
  playVoiceNotification(VOICE_FLIGHT_CTRL_READY);

  pinMode(CTRL_MODE_PIN, INPUT_PULLUP);
  pinMode(NAV_MODE_PIN, INPUT_PULLUP);
  pinMode(MTS102_SW_PIN, INPUT);
  controllerPrefs.begin("controller", false);
  settingsVolume = constrain(controllerPrefs.getUChar("volume", 20), (uint8_t)0, (uint8_t)100);
  settingsBrightness = constrain(controllerPrefs.getUChar("brightness", 100), (uint8_t)25, (uint8_t)100);
  settingsDarkMode = controllerPrefs.getBool("dark", true);
  Audio_SetVolume(settingsVolume);
  startAudioPump();                // tieng "ready" phat ngay, khong doi init ESP-NOW/TFT/OLED xong
  initEspNowController();
  Display_Init(settingsDarkMode);
  OLED_Init();
  // TFT: logo to-nho-to-nho 2.5s, dong thoi OLED chay chu RESHAPE LAB.
  Display_BootLogo(2500, OLED_BootMarqueeFrame);
  stopAudioPump();                 // tu day loop() tu goi Audio_Update()
  OLED_StartTask();                // roi OLED chuyen sang logo + thong so, TFT vao dashboard

  Serial.println("[CTRL] Hold SW for 5s to switch mode");
}

void loop() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      if (simTelBuf.startsWith("$SIMTEL,")) applySimTelLine(simTelBuf);
      simTelBuf = "";
    } else if (c != '\r') {
      simTelBuf += c;
      if (simTelBuf.length() > SIM_TEL_BUF_MAX) simTelBuf = "";
    }
  }

  Audio_Update();

  bool pressed = (digitalRead(CTRL_MODE_PIN) == LOW);

  if (pressed && !holdDetected) {
    holdDetected = true;
    holdStartMs = millis();
  }

  if (!pressed && holdDetected) {
    holdDetected = false;
    holdStartMs = 0;
  }

  if (holdDetected && (millis() - holdStartMs >= CTRL_MODE_HOLD_MS)) {
    applyMode(!modeEspNow);
    holdDetected = false;
    holdStartMs = 0;
    delay(100);
  }

  // Read sticks and build command
  RCCommand cmd = buildCommand();

  const bool navSwitchPressed = digitalRead(NAV_MODE_PIN) == LOW;
  const bool menuSwitchActive = digitalRead(MTS102_SW_PIN) == HIGH;
  if (menuSwitchActive != inSettingsMenu) {
    inSettingsMenu = menuSwitchActive;
    menuPitchLatched = false;
    menuNavPrev = navSwitchPressed;
  }

  if (inSettingsMenu) {
    if (abs(cmd.pitch) < 300) menuPitchLatched = false;
    if (!menuPitchLatched && cmd.pitch > 500) {
      settingsItem = settingsItem == 0 ? 2 : settingsItem - 1;
      menuPitchLatched = true;
    } else if (!menuPitchLatched && cmd.pitch < -500) {
      settingsItem = (settingsItem + 1) % 3;
      menuPitchLatched = true;
    }
    if (navSwitchPressed && !menuNavPrev) adjustSelectedSetting();
    menuNavPrev = navSwitchPressed;
  } else {
    if (navSwitchPressed && !navHoldDetected) {
      navHoldDetected = true;
      navHoldStartMs = millis();
    }

    if (!navSwitchPressed) {
      navHoldDetected = false;
      navHoldStartMs = 0;
      navSwitchPrev = true;
    }

    if (navSwitchPressed && navHoldDetected &&
        millis() - navHoldStartMs >= NAV_MODE_HOLD_MS) {
      if (navSwitchPrev) {
        navMode = (navMode == 0) ? RF_AUX_POSHOLD :
                  (navMode == RF_AUX_POSHOLD) ? RF_AUX_RTH :
                  (navMode == RF_AUX_RTH) ? RF_AUX_GEOFENCE : 0;

        Serial.printf("[CTRL] navigation mode=0x%02X\n", navMode);
        if (navMode == RF_AUX_RTH) {
          playVoiceNotification(VOICE_RTH_ACTIVATED);
        } else if (navMode == 0) {
          playVoiceNotification(VOICE_ANGLE_MODE);
        }
        navSwitchPrev = false;   // khÃ³a láº¡i cho Ä‘áº¿n khi nháº£ nÃºt
      }
    }
  }

  DisplayState ds{};
  ds.inSettingsMenu = inSettingsMenu;
  ds.settingsItem = settingsItem;
  ds.volume = settingsVolume;
  ds.brightness = settingsBrightness;
  ds.darkMode = settingsDarkMode;
  ds.armed = sendingEnabled;
  ds.useNrf24 = !modeEspNow;
  ds.droneLinkFresh = (millis() - gTelemetryLastMs) < 500;
  const uint32_t sendTotal = gSendOkCount + gSendFailCount;
  ds.linkQualityPct = (sendTotal > 0) ? (100.0f * gSendOkCount) / sendTotal : 0.0f;
  ds.droneRoll = gTelemetry.roll / 10.0f;
  ds.dronePitch = gTelemetry.pitch / 10.0f;
  ds.droneYaw = gTelemetry.yaw / 10.0f;
  ds.m1 = gTelemetry.m1; ds.m2 = gTelemetry.m2; ds.m3 = gTelemetry.m3; ds.m4 = gTelemetry.m4;
  ds.droneArmed = gTelemetry.armed != 0;
  ds.droneAltitudeM = gTelemetry.gpsFix ? gTelemetry.gpsAltitudeM : -1.0f;
  ds.droneSpeedKmh = gTelemetry.gpsFix ? gTelemetry.gpsSpeedKmh : -1.0f;
  ds.gpsFix = gTelemetry.gpsFix;
  ds.gpsSatellites = gTelemetry.gpsSatellites;
  ds.navModeRequested = navMode;
  ds.navMode = gTelemetry.navMode;
  ds.navError = gTelemetry.navError;
  static bool prevLinkFresh = true;
  if (ds.droneLinkFresh != prevLinkFresh) {
    if (ds.droneLinkFresh) {
      playVoiceNotification(VOICE_TELEMETRY_RECOVERED);
    } else {
      playVoiceNotification(VOICE_TELEMETRY_LOST);
    }
    prevLinkFresh = ds.droneLinkFresh;
  }
  if (gConsecutiveSendFailCount >= 5 && !gSendErrorAudioLatched) {
    gSendErrorAudioLatched = true;
  }
  Display_Update(ds);

  // START/END detection with hold timer for reliability
  static uint32_t startCandidateMs = 0;
  static uint32_t endCandidateMs = 0;
  uint32_t now = millis();

  bool startCond = (abs(cmd.throttle - START_THROTTLE) <= TOLERANCE) && (cmd.yaw >= YAW_ENDPOINT_THRESHOLD);
  bool endCond = (abs(cmd.throttle - END_THROTTLE) <= TOLERANCE) && (cmd.yaw <= -YAW_ENDPOINT_THRESHOLD);

  if (!sendingEnabled) {
    if (startCond) {
      if (startCandidateMs == 0) startCandidateMs = now;
      else if (now - startCandidateMs >= START_HOLD_MS) {
        sendingEnabled = true;
        navMode = 0;
        disarmPending = false;
        startCandidateMs = 0;
        endCandidateMs = 0;
        Serial.println("[CTRL] START");
        playVoiceNotification(VOICE_ARMED);
      }
    } else {
      startCandidateMs = 0;
    }
  } else {
    if (endCond) {
      if (endCandidateMs == 0) endCandidateMs = now;
      else if (now - endCandidateMs >= END_HOLD_MS) {
        sendingEnabled = false;
        disarmPending = true;
        disarmPendingUntilMs = now + 500;
        endCandidateMs = 0;
        startCandidateMs = 0;
        Serial.println("[CTRL] END");
        playVoiceNotification(VOICE_DISARMED);
      }
    } else {
      endCandidateMs = 0;
    }
  }

  // --- Throttle hold: chi hoat dong khi da ARMED ---
  static float throttleHoldValue = (float)THROTTLE_ARM_BASE;
  static uint32_t lastThrottleUpdateMs = 0;
  static bool wasArmedPrev = false;

  if (sendingEnabled && !wasArmedPrev) {
    // Vua ARM trong vong lap nay: khoi tao ve muc idle an toan, KHONG theo vi tri J1
    throttleHoldValue = (float)THROTTLE_ARM_BASE;
    lastThrottleUpdateMs = now;
  }
  wasArmedPrev = sendingEnabled;

  if (sendingEnabled) {
    uint32_t dtMs = now - lastThrottleUpdateMs;
    lastThrottleUpdateMs = now;
    float dt = dtMs / 1000.0f;
    if (dt > 0.2f) dt = 0.2f; // tranh nhay lon neu vong lap bi cham mot lan

    if (abs(gThrottleDeflection) > THROTTLE_DEADZONE) {
      // Day J1 len (+) => tang throttle, keo xuong (-) => giam throttle.
      // Toc do ti le voi do lech (day cang manh, doi cang nhanh).
      float throttleNorm = gThrottleDeflection / 1000.0f; // -1..1
      throttleHoldValue += throttleNorm * THROTTLE_RATE_PER_SEC * dt;
    }
    // Trong deadzone => khong lam gi ca, GIU NGUYEN muc throttle hien tai.

    if (throttleHoldValue > 2000.0f) throttleHoldValue = 2000.0f;
    if (throttleHoldValue < 1000.0f) throttleHoldValue = 1000.0f;

    cmd.throttle = (int16_t)(throttleHoldValue + 0.5f);
  }

  static uint32_t throttleWarningCandidateMs = 0;
  static bool throttleWarningPlayed = false;
  if (sendingEnabled && throttleHoldValue > 1900.0f) {
    if (throttleWarningCandidateMs == 0) {
      throttleWarningCandidateMs = now;
    } else if (!throttleWarningPlayed &&
               now - throttleWarningCandidateMs >= 2000) {
      playVoiceNotification(VOICE_THROTTLE_WARNING);
      throttleWarningPlayed = true;
    }
  } else {
    throttleWarningCandidateMs = 0;
    throttleWarningPlayed = false;
  }
  // Khi chua armed: cmd.throttle giu nguyen gia tri vi tri tho (dung cho START check o tren)

  cmd.aux = sendingEnabled ? (RF_AUX_ARMED | navMode) : 0;
  cmd.checksum = computeChecksum(cmd);

  sendControlWithCmd(cmd);

  OledInfo oledInfo{};
  oledInfo.sendOk = gSendOkCount;
  oledInfo.sendFail = gSendFailCount;
  oledInfo.throttleUs = static_cast<uint16_t>(cmd.throttle);
  oledInfo.telemetryAgeMs = gTelemetryLastMs == 0 ? 0xFFFFFFFFu : millis() - gTelemetryLastMs;
  oledInfo.armed = sendingEnabled;
  oledInfo.useNrf24 = !modeEspNow;
  OLED_SetInfo(oledInfo);

  delay(20);
}
