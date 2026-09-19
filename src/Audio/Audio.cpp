#include "Audio.h"
#include <string.h>
#include <math.h>
#include <driver/i2s.h>

// ---------------------------------------------------------------------------
// 48kHz la sample rate chuan, nam trong bang PLL cua PCM5100A khi chan MC
// (SCK) duoc noi xuong GND.
// 16-bit stereo -> BCK = 32 * fs = 1.536MHz.
// Neu loa cam im ru (DAC khong khoa duoc PLL o ti le 32fs), doi
// AUDIO_BITS sang I2S_BITS_PER_SAMPLE_32BIT de len 64fs - xem README cuoi file.
// ---------------------------------------------------------------------------
static const uint32_t kSampleRate = 48000;
static const i2s_port_t kPort = (i2s_port_t)AUDIO_I2S_PORT;

static const size_t kFramesPerChunk = 128;          // 128 frame stereo / lan ghi
static const size_t kSamplesPerChunk = kFramesPerChunk * 2;

// Ramp vao/ra ~3ms de khong bi "bup" o loa Class-D.
static const uint32_t kRampSamples = 144;

// ---------------------------------------------------------------------------
// Song VUONG, khong phai sin. Beeper cua EdgeTX/RadioMaster la mot bo tao
// xung PWM don gian - am thanh dan, "tit tit" dien tu, khong phai tieng nhac.
// Do la ly do minh bo hoan toan bang sin truoc day.
// ---------------------------------------------------------------------------
struct Note {
  uint16_t freq; // Hz, 0 = im lang
  uint16_t ms;
};

// loop = true: het pattern thi quay lai note 0, phat mai cho den khi
// Audio_Play() cue khac hoac Audio_Stop() duoc goi. Dung cho canh bao
// nguy cap kieu "mat telemetry" / "pin yeu" - dac trung cua EdgeTX: canh
// bao khong keu 1 lan roi thoi, no keu lien tuc cho den khi het nguy.
struct Pattern {
  const Note *notes;
  uint8_t count;
  bool loop;
};

// --- Cac cue, phong theo quy uoc beeper cua EdgeTX/RadioMaster ---

// Khoi dong: 3 tieng "tit" ngan, tang dan cao do (giong jingle khoi dong TX).
static const Note kBoot[] = {
  {1500, 55}, {0, 25}, {1800, 55}, {0, 25}, {2300, 80},
};

// Switch/arm bat: 2 tieng "tit" ngan tang dan - xac nhan don gian.
static const Note kArm[] = {
  {1800, 45}, {0, 25}, {2400, 65},
};

// Switch/disarm: 2 tieng "tit" ngan giam dan - doi xung voi Arm.
static const Note kDisarm[] = {
  {2400, 45}, {0, 25}, {1800, 65},
};

// Mat telemetry: cum 3 tieng gap + khoang lang dai, LAP LAI vo han.
// Day chinh la mau "canh bao mat tin hieu" kinh dien tren EdgeTX.
static const Note kLinkLost[] = {
  {1200, 90}, {0, 90}, {1200, 90}, {0, 90}, {1200, 90}, {0, 500},
};

// Co lai ket noi: 1 tieng "tit" xac nhan, khong lap.
static const Note kLinkOk[] = {
  {1600, 90},
};

// Doi che do (nav mode / ESP-NOW <-> nRF24): 1 tieng "tach" rat ngan.
static const Note kModeChange[] = {
  {2000, 40},
};

// Loi nghiem trong / gui that bai lien tuc: coi bao 2 tan so xen ke,
// LAP LAI vo han - giong canh bao pin yeu tren may bay EdgeTX.
static const Note kError[] = {
  {1000, 120}, {0, 15}, {1500, 120}, {0, 15},
};

static const Pattern kPatterns[AUDIO_CUE_COUNT] = {
  {nullptr, 0, false},                                                        // NONE
  {kBoot,       sizeof(kBoot) / sizeof(Note),       false},
  {kArm,        sizeof(kArm) / sizeof(Note),        false},
  {kDisarm,     sizeof(kDisarm) / sizeof(Note),     false},
  {kLinkLost,   sizeof(kLinkLost) / sizeof(Note),   true},
  {kLinkOk,     sizeof(kLinkOk) / sizeof(Note),     false},
  {kModeChange, sizeof(kModeChange) / sizeof(Note), false},
  {kError,      sizeof(kError) / sizeof(Note),      true},
};

// ---------------------------------------------------------------------------
static bool sReady = false;
static uint8_t sVolume = 60;

static AudioCue sCurrentCue = AUDIO_CUE_NONE;
static AudioCue sQueuedCue = AUDIO_CUE_NONE;
static uint8_t sNoteIndex = 0;
static uint32_t sNoteSamplesLeft = 0;
static uint32_t sNoteSamplesTotal = 0;
static uint32_t sPhase = 0;
static uint32_t sPhaseInc = 0;
static bool sDmaCleared = true;

// --- phat thoai (WAV thu san, nhung trong flash) ---
static VoiceClip sCurrentVoice = VOICE_NONE;
static VoiceClip sQueuedVoice = VOICE_NONE;
static const uint8_t *sVoiceCursor = nullptr;
static size_t sVoiceBytesLeft = 0;
static uint32_t sCurrentRate = kSampleRate;

// --- dieu khien chan SD (shutdown, active-low) cua tang Class-D ---
static bool sAmpOn = false;
static uint32_t sAmpOnMs = 0;
static uint32_t sLastSoundMs = 0;

static void ampSet(bool on) {
#if AUDIO_PIN_SD >= 0
  if (sAmpOn == on) return;
  digitalWrite(AUDIO_PIN_SD, on ? HIGH : LOW);
  if (on) sAmpOnMs = millis();
  sAmpOn = on;
#else
  (void)on; // SD noi cung len VCC -> amp luon bat, khong lam gi
#endif
}

static int16_t sBuf[kSamplesPerChunk];
static size_t sPendingBytes = 0;
static size_t sPendingOffset = 0;

// ---------------------------------------------------------------------------
static void startNote(const Note &n) {
  sNoteSamplesTotal = (uint32_t)((uint64_t)n.ms * kSampleRate / 1000u);
  if (sNoteSamplesTotal == 0) sNoteSamplesTotal = 1;
  sNoteSamplesLeft = sNoteSamplesTotal;
  sPhase = 0;
  sPhaseInc = n.freq ? (uint32_t)(((uint64_t)n.freq << 32) / kSampleRate) : 0;
}

static bool advanceCue() {
  const Pattern &p = kPatterns[sCurrentCue];
  ++sNoteIndex;
  if (sNoteIndex >= p.count) {
    if (p.loop) {
      sNoteIndex = 0; // canh bao dang lap: quay lai dau, phat mai
    } else {
      sCurrentCue = AUDIO_CUE_NONE;
      return false;
    }
  }
  startNote(p.notes[sNoteIndex]);
  return true;
}

static bool beginCue(AudioCue cue) {
  if (cue == AUDIO_CUE_NONE || cue >= AUDIO_CUE_COUNT) return false;
  const Pattern &p = kPatterns[cue];
  if (p.notes == nullptr || p.count == 0) return false;
  sCurrentCue = cue;
  sNoteIndex = 0;
  startNote(p.notes[0]);
  sDmaCleared = false;
  return true;
}

// --- phat thoai: doc thang tu vung flash da nhung, khong copy vao RAM ---
static size_t sVoiceTotalSamples = 0;

static void setSampleRate(uint32_t rate) {
  if (sCurrentRate == rate) return;
  i2s_set_sample_rates(kPort, rate);
  sCurrentRate = rate;
}

static bool beginVoice(VoiceClip clip) {
  if (clip == VOICE_NONE || clip >= VOICE_COUNT) return false;
  const VoiceEntry &e = kVoiceTable[clip];
  if (e.data == nullptr || e.bytes == 0) return false;

  // Thoai uu tien tuyet doi: cat ngang beeper dang keu ngay lap tuc.
  sCurrentCue = AUDIO_CUE_NONE;
  sNoteSamplesLeft = 0;

  sCurrentVoice = clip;
  sVoiceCursor = e.data;
  sVoiceBytesLeft = e.bytes;
  sVoiceTotalSamples = e.bytes / sizeof(int16_t);

  setSampleRate(e.sampleRate);
  sDmaCleared = false;
  return true;
}

// Sinh mot chunk tu clip thoai dang phat. Fade in/out ~144 mau (~6ms o
// 24kHz) o dau/cuoi CA CLIP (khong phai tung note nhu ben tone) de tranh
// tieng "bup" khi cat vao/ra giua doan im lang cua file thu am.
static size_t renderVoiceChunk() {
  size_t framesAvail = sVoiceBytesLeft / sizeof(int16_t);
  size_t framesLeft = kSamplesPerChunk / 2;
  size_t n = framesAvail < framesLeft ? framesAvail : framesLeft;
  if (n == 0) return 0;

  const int16_t *src = (const int16_t *)sVoiceCursor;
  size_t samplesDone = sVoiceTotalSamples - framesAvail;
  uint32_t ramp = kRampSamples;
  if (sVoiceTotalSamples > 0 && ramp > sVoiceTotalSamples / 4) {
    ramp = (uint32_t)(sVoiceTotalSamples / 4);
  }

  size_t written = 0;
  for (size_t i = 0; i < n; ++i) {
    int32_t v = src[i];
    size_t pos = samplesDone + i;
    size_t remaining = framesAvail - i;
    if (ramp > 0) {
      if (pos < ramp) {
        v = v * (int32_t)pos / (int32_t)ramp;
      } else if (remaining < ramp) {
        v = v * (int32_t)remaining / (int32_t)ramp;
      }
    }
    v = v * (int32_t)sVolume / 100;
    int16_t s = (int16_t)v;
    sBuf[written++] = s; // L
    sBuf[written++] = s; // R
  }
  sVoiceCursor += n * sizeof(int16_t);
  sVoiceBytesLeft -= n * sizeof(int16_t);
  return written;
}


// Sinh mot chunk vao sBuf, tra ve so SAMPLE (khong phai frame) da sinh.
static size_t renderChunk() {
  size_t written = 0;
  while (written < kSamplesPerChunk) {
    if (sNoteSamplesLeft == 0) {
      if (!advanceCue()) break;
    }
    size_t framesLeft = (kSamplesPerChunk - written) / 2;
    size_t n = sNoteSamplesLeft < framesLeft ? sNoteSamplesLeft : framesLeft;

    for (size_t i = 0; i < n; ++i) {
      int16_t s = 0;
      if (sPhaseInc) {
        // dinh bien do muc tieu (theo volume), roi ap envelope, roi moi
        // gan dau +/- theo pha -> song vuong "tit tit" dac trung beeper.
        int32_t peak = (int32_t)32767 * (int32_t)sVolume / 100;

        uint32_t pos = sNoteSamplesTotal - sNoteSamplesLeft + i;
        uint32_t ramp = kRampSamples;
        if (ramp > sNoteSamplesTotal / 4) ramp = sNoteSamplesTotal / 4;
        if (ramp > 0) {
          uint32_t remaining = sNoteSamplesLeft - i;
          if (pos < ramp) {
            peak = peak * (int32_t)pos / (int32_t)ramp;
          } else if (remaining < ramp) {
            peak = peak * (int32_t)remaining / (int32_t)ramp;
          }
        }

        s = ((sPhase & 0x80000000UL) == 0) ? (int16_t)peak : (int16_t)(-peak);
        sPhase += sPhaseInc;
      }
      sBuf[written++] = s; // L
      sBuf[written++] = s; // R
    }
    sNoteSamplesLeft -= n;
  }
  return written;
}

// ---------------------------------------------------------------------------
bool Audio_Init() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kSampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true; // tu dien 0 khi underrun -> khong ru re
  cfg.fixed_mclk = 0;

  esp_err_t err = i2s_driver_install(kPort, &cfg, 0, nullptr);
  if (err != ESP_OK) {
    Serial.printf("[AUDIO] i2s_driver_install failed: %d\n", (int)err);
    return false;
  }

  // memset 0xFF => moi truong = -1 = I2S_PIN_NO_CHANGE.
  // Lam kieu nay de khong phu thuoc vao viec struct co truong mck_io_num hay khong.
  i2s_pin_config_t pins;
  memset(&pins, 0xFF, sizeof(pins));
  pins.bck_io_num = AUDIO_PIN_BCK;
  pins.ws_io_num = AUDIO_PIN_WS;
  pins.data_out_num = AUDIO_PIN_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  err = i2s_set_pin(kPort, &pins);
  if (err != ESP_OK) {
    Serial.printf("[AUDIO] i2s_set_pin failed: %d\n", (int)err);
    i2s_driver_uninstall(kPort);
    return false;
  }

  i2s_zero_dma_buffer(kPort);

#if AUDIO_PIN_SD >= 0
  pinMode(AUDIO_PIN_SD, OUTPUT);
  digitalWrite(AUDIO_PIN_SD, LOW); // khoi dong o trang thai cam
  sAmpOn = false;
#else
  sAmpOn = true; // chan SD noi cung len VCC
#endif

  sReady = true;
  Serial.printf("[AUDIO] PCM5100A ready BCK=%d WS=%d DIN=%d SD=%d @%luHz\n",
                AUDIO_PIN_BCK, AUDIO_PIN_WS, AUDIO_PIN_DIN, (int)AUDIO_PIN_SD,
                (unsigned long)kSampleRate);
  return true;
}

void Audio_SetVolume(uint8_t percent) {
  sVolume = percent > 100 ? 100 : percent;
}

bool Audio_IsBusy() {
  return sCurrentCue != AUDIO_CUE_NONE || sCurrentVoice != VOICE_NONE || sPendingBytes > 0;
}

void Audio_Play(AudioCue cue) {
  if (!sReady || cue == AUDIO_CUE_NONE) return;
  if (sCurrentCue == cue) return; // dang phat chinh no roi, bo qua
  sQueuedCue = cue;
}

void Audio_PlayVoice(VoiceClip clip) {
  if (!sReady || clip == VOICE_NONE || clip >= VOICE_COUNT) return;
  if (sCurrentVoice == clip) return; // dang phat chinh no roi, bo qua
  const VoiceEntry &e = kVoiceTable[clip];
  Serial.printf("[AUDIO] voice %d: data=%p bytes=%u rate=%lu\n",
                (int)clip, (const void *)e.data, (unsigned)e.bytes,
                (unsigned long)e.sampleRate);
  sQueuedVoice = clip;
}

void Audio_Stop() {
  if (!sReady) return;
  sCurrentCue = AUDIO_CUE_NONE;
  sQueuedCue = AUDIO_CUE_NONE;
  sCurrentVoice = VOICE_NONE;
  sQueuedVoice = VOICE_NONE;
  sNoteSamplesLeft = 0;
  sVoiceBytesLeft = 0;
  sPendingBytes = 0;
  sPendingOffset = 0;
  setSampleRate(kSampleRate);
  i2s_zero_dma_buffer(kPort);
  sDmaCleared = true;
  ampSet(false);
}

void Audio_Update() {
  if (!sReady) return;

  // 1) Con du tu lan truoc chua ghi het -> uu tien day not.
  if (sPendingBytes > 0) {
    size_t wrote = 0;
    i2s_write(kPort, (const uint8_t *)sBuf + sPendingOffset, sPendingBytes, &wrote, 0);
    sPendingOffset += wrote;
    sPendingBytes -= wrote;
    if (sPendingBytes > 0) return; // DMA day, quay lai vong sau
  }

  // 2) Thoai co uu tien tuyet doi: neu co yeu cau, cat ngang beeper ngay.
  if (sQueuedVoice != VOICE_NONE) {
    VoiceClip v = sQueuedVoice;
    sQueuedVoice = VOICE_NONE;
    if (!beginVoice(v)) {
      Serial.printf("[AUDIO] beginVoice(%d) FAILED - data null hoac bytes=0\n", (int)v);
    }
  } else if (sQueuedCue != AUDIO_CUE_NONE && sCurrentVoice == VOICE_NONE) {
    // Tone chi duoc bat dau khi khong co thoai nao dang chay.
    AudioCue c = sQueuedCue;
    sQueuedCue = AUDIO_CUE_NONE;
    setSampleRate(kSampleRate);
    beginCue(c);
  }

  if (sCurrentVoice == VOICE_NONE && sCurrentCue == AUDIO_CUE_NONE) {
    if (!sDmaCleared) {
      i2s_zero_dma_buffer(kPort);
      sDmaCleared = true;
    }
    // Tat amp sau mot khoang im lang -> het xi nen cua Class-D khi khong tai.
    if (AUDIO_AMP_IDLE_OFF_MS > 0 && sAmpOn &&
        (millis() - sLastSoundMs) > AUDIO_AMP_IDLE_OFF_MS) {
      ampSet(false);
    }
    return;
  }

  // Bat amp truoc, cho no on dinh roi moi day mau vao.
  if (!sAmpOn) {
    ampSet(true);
    return;
  }
  if ((millis() - sAmpOnMs) < AUDIO_AMP_WAKE_MS) return;

  sLastSoundMs = millis();

  // 3) Sinh va day toi da vai chunk, dung ngay khi DMA day.
  for (int guard = 0; guard < 4; ++guard) {
    size_t samples;
    if (sCurrentVoice != VOICE_NONE) {
      samples = renderVoiceChunk();
      if (samples == 0) {
        sCurrentVoice = VOICE_NONE;
        setSampleRate(kSampleRate); // tra ve nhip mac dinh cho lan tone sau
        break;
      }
    } else {
      samples = renderChunk();
      if (samples == 0) break;
    }

    size_t bytes = samples * sizeof(int16_t);
    size_t wrote = 0;
    i2s_write(kPort, (const uint8_t *)sBuf, bytes, &wrote, 0);
    if (wrote < bytes) {
      sPendingOffset = wrote;
      sPendingBytes = bytes - wrote;
      return;
    }
  }
}