#pragma once
#include <Arduino.h>
#include "VoiceData.h"

// ---------------------------------------------------------------------------
// WeActStudio I2S Speaker Module V1 (PCM5100A DAC + 2 x Class-D PA)
//
// Silkscreen tren board:  VCC  GND  SD  MC  BCK  DIN  WS
//   WS  = word select = LRCK cua PCM5100A (chon kenh L/R)
//   MC  = master clock = SCK cua PCM5100A
//   SD  = shutdown cua tang khuech dai, active-low -> phai HIGH moi co tieng
//
// MC PHAI noi xuong GND. PCM5100A khoi dong o che do cho SCK ngoai; khi thay
// SCK bi keo mass no chuyen sang PLL mode va tu sinh system clock tu BCK.
// Ke qua: sample rate phai la gia tri chuan (8k/16k/32k/44k1/48k), khong duoc
// chon tan so la.
//
//   Module    ESP32 DevKit V1 (env: esp32_controller)
//   ------------------------------------------------
//   VCC       5V   (rail nguon, kem tu 470uF; KHONG lay tu chan 3V3)
//   GND       GND
//   SD        GPIO2  (hoac noi cung len VCC neu khong muon mute bang phan mem)
//   MC        GND
//   BCK       GPIO16
//   DIN       GPIO15
//   WS        GPIO17
//   SPK_L+/-  loa 4 ohm
//
// Neu ban da han sang chan khac, chi can sua cac define duoi day.
// ---------------------------------------------------------------------------
#ifndef AUDIO_PIN_BCK
#define AUDIO_PIN_BCK 16
#endif
#ifndef AUDIO_PIN_WS
#define AUDIO_PIN_WS 17
#endif
#ifndef AUDIO_PIN_DIN
#define AUDIO_PIN_DIN 15
#endif
// Dat -1 neu ban noi cung chan SD len VCC.
#ifndef AUDIO_PIN_SD
#define AUDIO_PIN_SD 2
#endif

// Tre giua luc bat amp va luc bat dau day mau, tranh tieng "bup" khi vao khoi.
#ifndef AUDIO_AMP_WAKE_MS
#define AUDIO_AMP_WAKE_MS 6
#endif

// Tat amp sau khi im lang bao lau (ms). 0 = khong bao gio tat.
#ifndef AUDIO_AMP_IDLE_OFF_MS
#define AUDIO_AMP_IDLE_OFF_MS 400
#endif

// Dung I2S port 1: port 0 hay bi cac thu vien khac (ADC/DAC noi bo) gianh mat.
#ifndef AUDIO_I2S_PORT
#define AUDIO_I2S_PORT 1
#endif

enum AudioCue : uint8_t {
  AUDIO_CUE_NONE = 0,
  AUDIO_CUE_BOOT,        // khoi dong xong
  AUDIO_CUE_ARM,         // START - 2 tieng len giong
  AUDIO_CUE_DISARM,      // END   - 2 tieng xuong giong
  AUDIO_CUE_LINK_LOST,   // mat ket noi drone
  AUDIO_CUE_LINK_OK,     // co lai ket noi
  AUDIO_CUE_MODE_CHANGE, // doi nav mode / doi ESP-NOW <-> nRF24
  AUDIO_CUE_ERROR,       // loi: gui that bai lien tuc, navError...
  AUDIO_CUE_COUNT
};

// Goi mot lan trong setup(), SAU Serial.begin().
// Tra ve false neu driver I2S khong cai dat duoc.
bool Audio_Init();

// Goi moi vong loop(). Khong bao gio chan (timeout = 0).
void Audio_Update();

// Xep mot cue vao hang doi. An toan khi goi lien tuc moi vong lap:
// neu cue dang phat trung voi cue moi thi bo qua, khong phat chong len.
void Audio_Play(AudioCue cue);

// Phat mot cau thoai (WAV thu san, nhung vao flash). CAT NGANG beeper
// dang keu (ke ca canh bao dang lap nhu LINK_LOST/ERROR) - giong thoai
// luon uu tien. Sau khi cau thoai phat xong, quay ve im lang; neu dieu
// kien canh bao van con dung, ben goi tu Audio_Play() lai de beeper tiep
// tuc (module nay khong tu nho lai trang thai canh bao truoc do).
// Goi lien tiep cung mot clip trong luc no dang phat se bi bo qua.
void Audio_PlayVoice(VoiceClip clip);

// Ngat am thanh ngay lap tuc (vi du truoc khi arm de tranh nhieu).
void Audio_Stop();

// 0..100. Mac dinh 60. Loa 4 ohm 2.8W rat to, dung de 100 trong nha.
void Audio_SetVolume(uint8_t percent);

// true neu dang co cue chay do.
bool Audio_IsBusy();