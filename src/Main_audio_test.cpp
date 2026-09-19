#include <Arduino.h>
#include "Audio/Audio.h"

static uint8_t sVolume = 30 ;

static void printMenu() {
  Serial.println();
  Serial.println("=== AUDIO TEST ===");
  Serial.println("1 BOOT  2 ARM  3 DISARM  4 LINK_LOST  5 LINK_OK  6 MODE  7 ERROR");
  Serial.println("a ARMED  d DISARMED  b BATTERY_LOW  r RTH  m ANGLE_MODE");
  Serial.println("s = toggle SD pin lien tuc (nghe tieng 'tach' bat/tat amp)");
  Serial.println("v/V = giam/tang am luong");
  Serial.println("==================");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[TEST] Bat dau...");

  bool ok = Audio_Init();
  Serial.printf("[TEST] Audio_Init() = %s\n", ok ? "OK" : "FAIL - kiem tra BCK/WS/DIN");

  Audio_SetVolume(sVolume);
  printMenu();
}

void loop() {
  Audio_Update();

  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case '1': Serial.println("-> BOOT");        Audio_Play(AUDIO_CUE_BOOT); break;
      case '2': Serial.println("-> ARM");         Audio_Play(AUDIO_CUE_ARM); break;
      case '3': Serial.println("-> DISARM");      Audio_Play(AUDIO_CUE_DISARM); break;
      case '4': Serial.println("-> LINK_LOST");   Audio_Play(AUDIO_CUE_LINK_LOST); break;
      case '5': Serial.println("-> LINK_OK");     Audio_Play(AUDIO_CUE_LINK_OK); break;
      case '6': Serial.println("-> MODE_CHANGE"); Audio_Play(AUDIO_CUE_MODE_CHANGE); break;
      case '7': Serial.println("-> ERROR");       Audio_Play(AUDIO_CUE_ERROR); break;
      case 'a': Serial.println("-> VOICE_ARMED");   Audio_PlayVoice(VOICE_ARMED); break;
      case 'd': Serial.println("-> VOICE_DISARMED");Audio_PlayVoice(VOICE_DISARMED); break;
      case 'b': Serial.println("-> VOICE_BATTERY_LOW"); Audio_PlayVoice(VOICE_BATTERY_LOW); break;
      case 'r': Serial.println("-> VOICE_RTH");     Audio_PlayVoice(VOICE_RTH_ACTIVATED); break;
      case 'm': Serial.println("-> VOICE_ANGLE_MODE"); Audio_PlayVoice(VOICE_ANGLE_MODE); break;
      case 'e': Serial.println("-> VOICE_BEEPER_ACTIVATED"); Audio_PlayVoice(VOICE_BEEPER_ACTIVATED); break;
      case 't': Serial.println("-> VOICE_THROTTLE_WARNING"); Audio_PlayVoice(VOICE_THROTTLE_WARNING); break;
      case 'f': Serial.println("-> VOICE_FLIGHT_CTRL_READY"); Audio_PlayVoice(VOICE_FLIGHT_CTRL_READY); break;
      case 'k': Serial.println("-> VOICE_TELEMETRY_RECOVERED"); Audio_PlayVoice(VOICE_TELEMETRY_RECOVERED); break;
      case 'l': Serial.println("-> VOICE_TELEMETRY_LOST"); Audio_PlayVoice(VOICE_TELEMETRY_LOST); break;
      case 's': {
        static bool toggled = false;
        toggled = !toggled;
        Serial.printf("[TEST] SD pin force %s (ap tai vao loa nghe tieng xi)\n",
                       toggled ? "HIGH" : "LOW");
#if AUDIO_PIN_SD >= 0
        digitalWrite(AUDIO_PIN_SD, toggled ? HIGH : LOW);
#endif
        break;
      }
      case 'v':
        sVolume = sVolume > 10 ? sVolume - 10 : 0;
        Audio_SetVolume(sVolume);
        Serial.printf("[TEST] volume = %d\n", sVolume);
        break;
      case 'V':
        sVolume = sVolume < 100 ? sVolume + 10 : 100;
        Audio_SetVolume(sVolume);
        Serial.printf("[TEST] volume = %d\n", sVolume);
        break;
      case '\r': case '\n': break;
      default: printMenu(); break;
    }
  }
}