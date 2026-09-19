#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Cac file .pcm trong src/voice_data/ duoc nhung thang vao firmware qua
// board_build.embed_files (xem platformio_voice_snippet.ini). Linker tao ra
// 2 symbol _start/_end cho moi file, tro thang vao vung flash chua du lieu -
// khong ton RAM, khong can SPIFFS/LittleFS.
//
// QUAN TRONG: ten symbol do objcopy sinh ra tu DUONG DAN tuong doi cua file,
// thay moi ky tu khong phai chu/so bang '_'. Voi cay thu muc:
//   src/voice_data/armed.pcm
// symbol se la _binary_src_voice_data_armed_pcm_start / _end.
// Neu build bao "undefined reference to `_binary_...`", mo file .elf bang:
//   xtensa-esp32-elf-nm .pio\build\esp32_controller\firmware.elf | findstr binary
// (Windows) hoac | grep binary (Linux/Mac) de xem ten symbol THAT SU la gi,
// roi sua lai cac dong extern ben duoi cho khop.
// ---------------------------------------------------------------------------
extern "C" {
  extern const uint8_t _binary_src_voice_data_armed_pcm_start[];
  extern const uint8_t _binary_src_voice_data_armed_pcm_end[];
  extern const uint8_t _binary_src_voice_data_disarmed_pcm_start[];
  extern const uint8_t _binary_src_voice_data_disarmed_pcm_end[];
  extern const uint8_t _binary_src_voice_data_battery_low_pcm_start[];
  extern const uint8_t _binary_src_voice_data_battery_low_pcm_end[];
  extern const uint8_t _binary_src_voice_data_rth_activated_pcm_start[];
  extern const uint8_t _binary_src_voice_data_rth_activated_pcm_end[];
  extern const uint8_t _binary_src_voice_data_angle_mode_pcm_start[];
  extern const uint8_t _binary_src_voice_data_angle_mode_pcm_end[];
  extern const uint8_t _binary_src_voice_data_beeper_activated_pcm_start[];
  extern const uint8_t _binary_src_voice_data_beeper_activated_pcm_end[];
  extern const uint8_t _binary_src_voice_data_throttle_warning_pcm_start[];
  extern const uint8_t _binary_src_voice_data_throttle_warning_pcm_end[];
  extern const uint8_t _binary_src_voice_data_flight_controller_ready_pcm_start[];
  extern const uint8_t _binary_src_voice_data_flight_controller_ready_pcm_end[];
  extern const uint8_t _binary_src_voice_data_telemetry_recovered_pcm_start[];
  extern const uint8_t _binary_src_voice_data_telemetry_recovered_pcm_end[];
  extern const uint8_t _binary_src_voice_data_telemetry_lost_pcm_start[];
  extern const uint8_t _binary_src_voice_data_telemetry_lost_pcm_end[];
}

enum VoiceClip : uint8_t {
  VOICE_NONE = 0,
  VOICE_ARMED,               // "Armed, ready for takeoff"
  VOICE_DISARMED,            // "System disarmed"
  VOICE_BATTERY_LOW,         // "Warning, battery low"
  VOICE_RTH_ACTIVATED,       // "Return to home activated"
  VOICE_ANGLE_MODE,          // "Angle mode engaged"
  VOICE_BEEPER_ACTIVATED,    // "Beeper activated"
  VOICE_THROTTLE_WARNING,    // "Throttle warning, please lower throttle"
  VOICE_FLIGHT_CTRL_READY,   // "Flight controller initialized, system ready"
  VOICE_TELEMETRY_RECOVERED, // "Telemetry recovered"
  VOICE_TELEMETRY_LOST,      // "Telemetry lost"
  VOICE_COUNT
};

struct VoiceEntry {
  const uint8_t *data;  // tro toi mau PCM 16-bit mono dau tien
  size_t bytes;         // tong so byte (chan, vi 16-bit)
  uint32_t sampleRate;  // Hz, lay tu file .wav goc (24000)
};

// Dinh nghia trong VoiceData.cpp - mang nay khop chi so voi enum VoiceClip
// (bo qua VOICE_NONE o vi tri 0).
extern const VoiceEntry kVoiceTable[VOICE_COUNT];