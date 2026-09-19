#include "VoiceData.h"

// Cong thuc size: end - start (linker dat 2 symbol nay khop chinh xac
// diem dau/cuoi vung du lieu duoc nhung, khong co padding gay lech).
#define VOICE_SIZE(name) \
  (size_t)(_binary_src_voice_data_##name##_pcm_end - _binary_src_voice_data_##name##_pcm_start)

const VoiceEntry kVoiceTable[VOICE_COUNT] = {
  /* VOICE_NONE         */ {nullptr, 0, 0},
  /* VOICE_ARMED         */ {_binary_src_voice_data_armed_pcm_start,         VOICE_SIZE(armed),         24000},
  /* VOICE_DISARMED      */ {_binary_src_voice_data_disarmed_pcm_start,      VOICE_SIZE(disarmed),      24000},
  /* VOICE_BATTERY_LOW   */ {_binary_src_voice_data_battery_low_pcm_start,   VOICE_SIZE(battery_low),   24000},
  /* VOICE_RTH_ACTIVATED */ {_binary_src_voice_data_rth_activated_pcm_start, VOICE_SIZE(rth_activated), 24000},
  /* VOICE_ANGLE_MODE    */ {_binary_src_voice_data_angle_mode_pcm_start,    VOICE_SIZE(angle_mode),    24000},
  /* VOICE_BEEPER_ACTIVATED    */ {_binary_src_voice_data_beeper_activated_pcm_start,    VOICE_SIZE(beeper_activated),    24000},
  /* VOICE_THROTTLE_WARNING    */ {_binary_src_voice_data_throttle_warning_pcm_start,    VOICE_SIZE(throttle_warning),    24000},
  /* VOICE_FLIGHT_CTRL_READY   */ {_binary_src_voice_data_flight_controller_ready_pcm_start, VOICE_SIZE(flight_controller_ready), 24000},
  /* VOICE_TELEMETRY_RECOVERED */ {_binary_src_voice_data_telemetry_recovered_pcm_start, VOICE_SIZE(telemetry_recovered), 24000},
  /* VOICE_TELEMETRY_LOST      */ {_binary_src_voice_data_telemetry_lost_pcm_start,      VOICE_SIZE(telemetry_lost),      24000},
};