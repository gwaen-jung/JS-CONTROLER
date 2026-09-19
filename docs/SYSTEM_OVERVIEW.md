CINQ — Tổng quan Tay cầm điều khiển & Flight Simulator Web
18 thg 9, 2026
Mục đích & phạm vi
CINQ có hai hệ thống chạy song song, không kết nối trực tiếp với nhau cho tới khi bổ sung liên kết bench-test ở §6:
1. Bộ điều khiển thật — ESP32 tay cầm ↔ ESP32 drone (kèm STM32F103 tính PID) — đây là hệ thống bay thật, dùng ESP-NOW/NRF24 làm kênh RF chính.
2. Flight simulator web (gcs_drone_simulator.html) — một "controller test rig": mô phỏng vật lý bay ngay trong trình duyệt (Three.js), dùng để kiểm tra firmware tay cầm (joystick, ngưỡng ga, logic ARM/START) trước khi hoặc không cần có drone thật.
Hai hệ thống tồn tại độc lập vì mục tiêu khác nhau: (1) là sản phẩm cuối, (2) là công cụ dev/test — cho phép luyện tập điều khiển và xác minh hành vi firmware mà không cần rủi ro bay thật.
Kiến trúc tổng thể — 4 thành phần, 3 đường dữ liệu tách biệt
Thành phần
Vai trò
Máy tính (web simulator)
Chỉ dev/test — không thuộc hệ thống bay thật
ESP32 tay cầm (main_esp32_controler.cpp)
Đọc joystick, gửi RC qua RF, nhận Telemetry ngược, vẽ TFT + OLED
ESP32 drone (main_esp32.cpp)
Đọc IMU, nhận RC, forward qua UART cho STM32, xuất DShot cho ESC, gửi Telemetry về tay cầm
STM32F103 (Flight.cpp/Motor.cpp)
Tính cascade PID, mixing động cơ, trả MotorMix qua UART
3 đường dữ liệu, không đường nào đi qua đường khác:
1. USB Serial — máy tính ↔ ESP32 tay cầm, có dây, ban đầu chỉ đọc (debug joystick), nay đã mở 2 chiều cho $SIMTEL (§6).
2. ESP-NOW / NRF24 — ESP32 tay cầm ↔ ESP32 drone, không dây, 2 chiều: RC (tay cầm → drone) và TelemetryPacket (drone → tay cầm).
3. UART nội bộ — ESP32 drone ↔ STM32, có dây, trong chính thân drone: forward RC packet xuống, nhận MotorMix lên.
Nhầm lẫn phổ biến nhất khi debug: tưởng web simulator có ảnh hưởng tới TFT tay cầm qua ESP-NOW — thực tế trước §6 thì không, hai hệ thống hoàn toàn song song.
Firmware tay cầm — main_esp32_controler.cpp (env esp32_controller)
Input: dual joystick ADC (throttle/yaw trên joystick 1, roll/pitch trên joystick 2, GPIO 34/35/32/33), 2 nút SW (25/13), toggle switch settings/dashboard (GPIO 36).
RF:
• Chuyển mode ESP-NOW ↔ NRF24 bằng giữ CTRL_MODE_PIN 5s (modeEspNow, mặc định true).
• onEspNowRecv() nhận TelemetryPacket (header 0xCD) từ drone, kiểm checksum, ghi vào gTelemetry + gTelemetryLastMs.
• Học MAC drone và gửi RC command đi (header RCCommand 0xBC, kích thước khác hẳn TelemetryPacket nên không thể lẫn).
Output khác:
• OLED (I2C, GPIO 21/22) qua OLEDAnimation.h.
• TFT ILI9341 320×240 (SPI, CS27/DC26/RST14) qua display.cpp — chi tiết ở §4.
• Audio: beeper cue + voice PCM (Audio/Audio.h) cho các cảnh báo/thông báo.
• Preferences lưu cấu hình bền (calib joystick, mode RF...).
Vòng lặp chính (loop()): đọc joystick → gửi RC → cập nhật DisplayState từ gTelemetry (roll/pitch/yaw chia lại /10.0f để về độ thực) → Display_Update() → Audio_Update().
TFT Display module — display.cpp
Đã tồn tại đầy đủ trong firmware, chạy trên ILI9341 320×240 qua TFT_eSPI:
• drawPFD() — Primary Flight Display / artificial horizon: sky/ground theo roll+pitch, alt, speed.
• drawCompass() — la bàn heading thật (N/S/E/W) theo droneYaw.
• drawVectorRotation() — hiển thị kết hợp roll+pitch dạng vector.
• drawThrottle() — 4 thanh throttle riêng từng động cơ (m1-m4).
• drawTopBar() — trạng thái kết nối, armed, loại link (ESP-NOW/NRF24).
• Màn settings (toggle bằng switch GPIO 36): volume, độ sáng, dark/light mode.
Nguồn dữ liệu duy nhất trước §6: DisplayState được đổ từ gTelemetry (ghi bởi onEspNowRecv()) — tức là toàn bộ cụm instrument này phụ thuộc 100% vào việc gói TelemetryPacket có tới được từ drone qua RF hay không. Đây chính là lý do khi RF có vấn đề, TFT đứng im dù code vẽ hoàn toàn đúng.
Flight simulator web — gcs_drone_simulator.html
Một file HTML độc lập (Three.js + Web Serial API), không cần build/nạp gì, chạy trực tiếp trong Chrome/Edge.
Nguồn input: DEMO MODE (tự phát sóng sin/cos để test) hoặc CONNECT USB — đọc debug serial thô (JOY1: throttle=.. JOY2: roll=..) do chính firmware tay cầm in ra.
Mô hình vật lý:
• Gate flightRunning — chưa bấm START thì không có lực nâng nào được tính, drone nằm im.
• THROTTLE_LIFT_THRESHOLD = 1400 — ga dưới ngưỡng này không sinh lực đẩy (khớp đặc tính ESC/động cơ thật).
• Trọng lực (WEIGHT_N) luôn kéo xuống; lực nâng (liftN) chỉ xuất hiện khi ga vượt ngưỡng và tăng dần tới LIFT_MAX_N.
• Gió (wind.fx/fz) tác động ngang khi đang bay; lực cản khí động (DRAG_LINEAR).
• G-force hiển thị: (ay + GRAVITY) / GRAVITY — 1.0g lúc hover, <1g khi rơi, >1g khi leo mạnh.
Luyện tập:
• 7 cột chướng ngại vật (2 cặp tạo "gate" hẹp) với va chạm vật lý thật (đẩy lùi, giảm vận tốc), đếm HITS.
• Chuyển góc nhìn Chase (thứ 3, theo sau) ↔ FPV (thứ 1, gắn mũi drone, có crosshair) bằng nút VIEW.
• Radar mini, mission panel, log panel — tiện theo dõi khi test.
Các chỉ số roll/pitch/yaw/G-force/HDG trên web là tự tính trong JS, không phải số đo IMU thật — dùng để luyện phản xạ điều khiển và test firmware joystick, không phản ánh trạng thái drone thật.
Liên kết bench-test mới — $SIMTEL qua USB Serial
Để tách bạch lỗi "TFT không cập nhật" giữa nguyên nhân RF (ESP-NOW) và nguyên nhân display code, đã mở USB Serial (trước đây chỉ đọc) thành 2 chiều:
• Web → board: mỗi ~100ms, web gửi dòng $SIMTEL,roll,pitch,yaw,m1,m2,m3,m4,armed,alt,spd\n (giá trị lấy từ chính mô phỏng vật lý của web) qua writer.write().
• Board: đọc Serial.available() trong loop(), parse dòng $SIMTEL,..., ghi thẳng vào gTelemetry/gTelemetryLastMs — đúng biến mà Display_Update() đang đọc — nên TFT vẽ y như đang nhận ESP-NOW thật.
Cách dùng để cô lập lỗi: rút nguồn drone, chỉ chạy web + CONNECT USB + START, lái joystick trên web.
• TFT phản hồi đúng → display.cpp không có vấn đề, lỗi nằm ở đường ESP-NOW thật.
• TFT vẫn đứng im → lỗi nằm sâu hơn, trong chính display.cpp hoặc phần cứng TFT/SPI.
Trạng thái hiện tại & việc đang debug
Đã xác nhận và sửa (drone/PID):
• Roll Ki đánh máy 0.40 → 0.040 (config.cpp).
• Setpoint roll/pitch lệch scale x10 giữa rc.roll_sp (1/10 độ) và imu.roll (độ thực) — đã chia lại trong Flight.cpp.
• Zero-point chốt theo tư thế bất kỳ lúc arm — đã thêm gate ±3° (UART.cpp).
• Đã thêm log tại 3 điểm nối (TX/RX telemetry, TFT init) để bisect nguyên nhân TFT không cập nhật.
Đang chờ kết quả (chưa xác nhận):
1. Log D1-D3 (Serial Monitor cả 2 board) — xem gói Telemetry có tới đúng chỗ không.
2. Test liên kết $SIMTEL — xem display.cpp có tự vẽ đúng khi có nguồn giả lập từ web không, độc lập với ESP-NOW.
Kết quả 2 test trên sẽ quyết định bước tiếp theo: nếu cả 2 đều "sạch" nhưng TFT vẫn im khi bay drone thật → khoanh vùng vào phần cứng RF/antenna hoặc nhiễu EMI (đã có lịch sử USART1/EMI conflict trước đó với dự án này).