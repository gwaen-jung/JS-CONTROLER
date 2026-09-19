> **Cập nhật:** đã có thêm 5 clip giọng nói mới (xem §6). `VoiceData.h`/`VoiceData.cpp`/`platformio.ini` **đã được sửa sẵn** trong bản đính kèm — Copilot không cần tự thêm symbol/enum/embed_files cho 5 clip này nữa, chỉ cần gọi `Audio_PlayVoice(...)` đúng chỗ theo §6.

# Brief: Tích hợp Audio (beeper cue + voice PCM) vào firmware tay cầm

**File đích:** `src/main_esp32_controler.cpp` (env `esp32_controller` trong `platformio.ini`)
**Module nguồn (đã test OK ở env `esp32_audio_test`):** `src/Audio/Audio.h`, `src/Audio/Audio.cpp`, `src/VoiceData.h`, `src/VoiceData.cpp`, `src/voice_data/*.pcm`

Module Audio đã được xác nhận hoạt động đúng (loa I2S PCM5100A, cue beeper + voice PCM đều phát được qua `esp32_audio_test`). Việc còn lại là **nối nó vào logic thật của tay cầm** thay vì bàn phím test thủ công.

---

## 0. BẮT BUỘC SỬA TRƯỚC — bug trong `platformio.ini`

`VoiceData.cpp` hiện **không bị loại trừ** khỏi `build_src_filter` của `[env:esp32_controller]` (không có dòng `-<VoiceData.cpp>`), nghĩa là nó *đã* được compile vào firmware tay cầm. Nhưng `board_build.embed_files` (5 file `.pcm`) hiện **chỉ khai báo cho `[env:esp32_audio_test]`**.

➡️ Hậu quả: build `esp32_controller` sẽ **fail ở bước link** với lỗi kiểu `undefined reference to _binary_src_voice_data_armed_pcm_start` ngay khi bật lại (hoặc thậm chí có thể đã fail sẵn trước khi thêm code gọi Audio, tuỳ trình liên kết có loại bỏ symbol không dùng hay không).

**Việc cần làm:** thêm khối sau vào `[env:esp32_controller]` trong `platformio.ini` (copy y hệt từ `[env:esp32_audio_test]`):

```ini
board_build.embed_files =
    src/voice_data/armed.pcm
    src/voice_data/disarmed.pcm
    src/voice_data/battery_low.pcm
    src/voice_data/rth_activated.pcm
    src/voice_data/angle_mode.pcm
```

Không cần sửa gì trong `VoiceData.h` — tên symbol linker sinh ra dựa trên đường dẫn tương đối `src/voice_data/...`, giữ nguyên đường dẫn là symbol khớp.

---

## 1. Kiểm tra chân GPIO — đã rà, KHÔNG xung đột

Đã đối chiếu `AUDIO_PIN_BCK=16`, `AUDIO_PIN_WS=17`, `AUDIO_PIN_DIN=15`, `AUDIO_PIN_SD=2` (định nghĩa trong `Audio.h`) với toàn bộ chân đang dùng thật trong `main_esp32_controler.cpp` (`CTRL_MODE_PIN=25`, `NAV_MODE_PIN=13`, joystick `32/33/34/35`), TFT (`19/23/18/27/26/14`) và OLED (`Wire.begin` I2C mặc định `21/22`) — **không trùng chân nào**.

Hai điểm lưu ý (không phải lỗi, chỉ để tránh nhầm lẫn khi đọc code):
- `config.h` có `#define UART_RX_PIN 16` / `UART_TX_PIN 17` trùng số với chân audio, nhưng `UART_Init()` (trong `UART/UART.cpp`) **không được gọi** ở đâu trong `main_esp32_controler.cpp`, nên không có xung đột runtime. Nếu sau này có ai thêm UART bridge vào bản tay cầm, cần đổi chân audio hoặc chân UART.
- Comment tại dòng khai báo `NAV_MODE_PIN` ghi "GPIO2" nhưng giá trị thật là `13` — comment cũ bị lệch, không phải bug, nhưng gây khó đọc.

GPIO2 (`AUDIO_PIN_SD`) là chân strapping lúc boot trên ESP32 — không có gì nối cứng vào chân này trong thiết kế hiện tại nên an toàn.

Flash: 5 file `.pcm` nhúng thẳng cộng lại khoảng 555 KB — không đáng kể so với flash 4MB của board `esp32dev`.

---

## 2. Thay đổi trong `main_esp32_controler.cpp`

### 2.1 Include + init
```cpp
#include "Audio/Audio.h"
```
Trong `setup()`, gọi **sau** `Serial.begin(115200); delay(500);` (đúng yêu cầu ghi trong `Audio.h`):
```cpp
bool audioOk = Audio_Init();
Serial.printf("[CTRL] Audio_Init() = %s\n", audioOk ? "OK" : "FAIL");
Audio_Play(AUDIO_CUE_BOOT);
```

### 2.2 Update loop
Trong `loop()`, gọi `Audio_Update()` — không chặn, gọi mỗi vòng, đặt ở đầu vòng lặp cùng chỗ với các update khác:
```cpp
Audio_Update();
```

### 2.3 Sự kiện ARM (dòng `Serial.println("[CTRL] START");`)
Thêm ngay sau dòng đó:
```cpp
Audio_PlayVoice(VOICE_ARMED);
```

### 2.4 Sự kiện DISARM (dòng `Serial.println("[CTRL] END");`)
Thêm ngay sau dòng đó:
```cpp
Audio_PlayVoice(VOICE_DISARMED);
```

### 2.5 Đổi chế độ RF (ESPNow ↔ NRF24) — trong `applyMode()`
Sau khi `applyMode()` chạy xong (trong `loop()`, chỗ xử lý hold 5s), thêm:
```cpp
Audio_Play(AUDIO_CUE_MODE_CHANGE);
```

### 2.6 Mất/có lại link telemetry drone (`ds.droneLinkFresh`)
Cần thêm biến static để phát hiện *cạnh* (edge), vì `droneLinkFresh` được tính lại mỗi vòng lặp:
```cpp
static bool prevLinkFresh = true; // giả định có link lúc mới boot để tránh bíp giả ở lần đầu
if (ds.droneLinkFresh != prevLinkFresh) {
  Audio_Play(ds.droneLinkFresh ? AUDIO_CUE_LINK_OK : AUDIO_CUE_LINK_LOST);
  prevLinkFresh = ds.droneLinkFresh;
}
```
Đặt đoạn này ngay sau khối gán `ds.*` hiện có, trước `Display_Update(ds)` hoặc sau đều được.

`AUDIO_CUE_LINK_LOST` là loop-cue (lặp vô hạn cho tới khi có cue khác) theo đúng comment trong `Audio.cpp` — không cần tự lặp lại lệnh gọi.

### 2.7 Đổi nav mode (biến `navMode`, trong khối xử lý `NAV_MODE_PIN`)
Ngay sau dòng `Serial.printf("[CTRL] navigation mode=0x%02X\n", navMode);`:
```cpp
Audio_Play(AUDIO_CUE_MODE_CHANGE);
if (navMode == RF_AUX_RTH) {
  Audio_PlayVoice(VOICE_RTH_ACTIVATED);
}
```
`Audio_PlayVoice` sẽ tự cắt ngang cue vừa gọi ở trên (voice có ưu tiên tuyệt đối theo thiết kế Audio.cpp) — không xung đột.

---

## 3. Điểm CẦN BẠN QUYẾT ĐỊNH (chưa tự suy đoán thay bạn)

1. **`VOICE_ANGLE_MODE` chưa có nơi gắn.** Trong code tay cầm hiện tại không có khái niệm "angle mode" tách riêng — chỉ có `navMode` cycle qua `0 → POSHOLD → RTH → GEOFENCE → 0`. Có 2 hướng hợp lý:
   - (a) Coi `navMode == 0` (chế độ mặc định, không pos-hold/rth/geofence) là "angle mode" → phát `VOICE_ANGLE_MODE` khi quay về 0.
   - (b) Bỏ qua clip này ở firmware tay cầm, để dành cho bên STM32 flight controller (nơi khái niệm angle mode/self-level thực sự tồn tại trong `Flight/`).
   Brief này **không tự chọn** vì ảnh hưởng tới trải nghiệm người dùng — bạn xác nhận rồi Copilot làm theo.

2. **`VOICE_BATTERY_LOW` chưa có nguồn dữ liệu.** `TelemetryPacket` (trong `RF_Protocol.h`) hiện không có trường điện áp pin — cả pin tay cầm lẫn pin drone đều chưa được đo. Cần bạn xác nhận: đo pin ở đâu (ADC pin nào trên tay cầm, hay đợi thêm trường `batteryVoltage` vào `TelemetryPacket` từ phía drone) trước khi Copilot có thể nối `AUDIO_CUE_ERROR`/`VOICE_BATTERY_LOW` vào một ngưỡng cụ thể.

3. **`AUDIO_CUE_ERROR`** hiện chưa gán cho sự kiện nào. Ứng viên hợp lý: `esp_now_add_peer()` thất bại lúc init, hoặc `gSendFailCount` tăng liên tục vượt ngưỡng trong X giây. Đây cũng là chỗ nên hỏi bạn trước khi Copilot tự chọn ngưỡng.

---

## 4. Gợi ý phong cách RadioMaster/EdgeTX (đã áp dụng ở trên)

- Sự kiện **một lần, quan trọng** (arm/disarm, chuyển RTH) → ưu tiên giọng nói (`Audio_PlayVoice`) vì đã có file thu sẵn, rõ ràng hơn tiếng bíp thuần.
- Sự kiện **liên tục, cần cảnh báo dai dẳng** (mất link) → dùng cue dạng loop (`AUDIO_CUE_LINK_LOST`), tự tắt khi có cue/voice khác đè lên — đúng logic EdgeTX "báo liên tục cho tới khi hết nguy" mà comment trong `Audio.cpp` đã mô tả.
- Sự kiện **xác nhận thao tác phụ** (đổi mode RF, đổi nav mode) → cue ngắn không lặp, không cần giọng nói trừ khi mode đó có clip riêng (RTH).

---

## 5. Danh sách file Copilot sẽ đụng vào

- `platformio.ini` — thêm `board_build.embed_files` cho `[env:esp32_controller]` (§0) — **đã sửa sẵn trong bản đính kèm, kèm cả 5 file .pcm mới ở §6**
- `src/main_esp32_controler.cpp` — toàn bộ thay đổi ở §2 và §6

Không cần sửa `Audio.cpp`, `Audio.h`, `VoiceData.h`, `VoiceData.cpp` — các file này đã đúng/đã cập nhật sẵn.

---

## 6. 5 clip giọng nói mới (vừa nhận, đã convert + gắn symbol sẵn)

File `.pcm` (16-bit mono 24kHz, đã convert đúng chuẩn) đã nằm trong `src/voice_data/`, symbol đã khai báo trong `VoiceData.h`/`.cpp`, đã thêm vào `board_build.embed_files` của cả 2 env. Enum mới trong `VoiceClip`:

| Enum | File .pcm | Nội dung | Chỗ gắn |
|---|---|---|---|
| `VOICE_FLIGHT_CTRL_READY` | `flight_controller_ready.pcm` | "Flight controller initialized, system ready" | Ghép vào **§2.1 (boot)** — gọi ngay sau `Audio_Play(AUDIO_CUE_BOOT)` trong `setup()`. Voice sẽ tự cắt ngang cue boot theo đúng cơ chế ưu tiên đã biết. |
| `VOICE_TELEMETRY_RECOVERED` | `telemetry_recovered.pcm` | "Telemetry recovered" | Ghép vào **§2.6 (edge-detect droneLinkFresh)** — nhánh `ds.droneLinkFresh == true`: gọi thêm `Audio_PlayVoice(VOICE_TELEMETRY_RECOVERED)` ngay sau `Audio_Play(AUDIO_CUE_LINK_OK)`. |
| `VOICE_TELEMETRY_LOST` | `telemetry_lost.pcm` | "Telemetry lost" | Ghép vào **§2.6** — nhánh `ds.droneLinkFresh == false`: gọi thêm `Audio_PlayVoice(VOICE_TELEMETRY_LOST)` ngay sau `Audio_Play(AUDIO_CUE_LINK_LOST)`. Lưu ý: cue `LINK_LOST` là loop-cue; voice sẽ phát 1 lần rồi cue lặp tiếp tục kêu phía sau (đúng hành vi mô tả trong `Audio.h`: "voice phát xong quay ve im lang, bên gọi tự gọi lại Audio_Play() để beeper tiếp tục" — ở đây không cần tự gọi lại vì cue LINK_LOST vẫn đang "chờ" trong hàng đợi loop, xác nhận lại hành vi thực tế khi test trên board). |
| `VOICE_THROTTLE_WARNING` | `throttle_warning.pcm` | "Throttle warning, please lower throttle" | **Cần bạn xác nhận ngưỡng.** Chưa có logic nào trong `main_esp32_controler.cpp` kiểm tra throttle vượt ngưỡng nguy hiểm. Gợi ý: kiểm tra trong nhánh `sendingEnabled` (đã ARM), nếu `throttleHoldValue` vượt một ngưỡng cao (vd > 1800) duy trì liên tục hơn N giây thì phát 1 lần (dùng cờ static để tránh lặp lại mỗi vòng lặp). Cần bạn chốt ngưỡng cụ thể trước khi Copilot code. |
| `VOICE_BEEPER_ACTIVATED` | `beeper_activated.pcm` | "Beeper activated" | **Chưa rõ sự kiện gắn.** Tên gợi ý tính năng kiểu EdgeTX "lost model beeper" (bật chuông tìm máy bay khi mất tích) — nhưng project hiện chưa có tính năng này (không có switch/lệnh nào tên "beeper" trong `main_esp32_controler.cpp` hay `RF_Protocol.h`). Bạn xác nhận: đây là tính năng mới cần thêm (kèm switch/nút bấm nào?), hay đơn giản chỉ là lời chào khi cue-beeper bắt đầu kêu lần đầu (vd lúc `AUDIO_CUE_LINK_LOST` bắt đầu)? Chưa map cho tới khi có câu trả lời.

Tóm lại: **3/5 clip mới** (`flight_controller_ready`, `telemetry_recovered`, `telemetry_lost`) có thể đưa thẳng cho Copilot làm theo bảng trên. **2/5 clip** (`throttle_warning`, `beeper_activated`) cần bạn quyết định thêm, giống 3 điểm mở ở §3.
