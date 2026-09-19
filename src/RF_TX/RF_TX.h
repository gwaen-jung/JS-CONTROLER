#ifndef RF_TX_H
#define RF_TX_H

#include <Arduino.h>
#include <Wire.h>
#include <RF24.h>
#include "../RF_Protocol.h" // RCCommand, RF_PIPE_ADDRESS, RF_PAYLOAD_SIZE dung chung voi RX
// STM32duino core KHÔNG tự include FreeRTOS qua Arduino.h như ESP32 -
// phải include tường minh ở đây, vì QueueHandle_t/TaskHandle_t bên dưới
// cần định nghĩa từ FreeRTOS mới hợp lệ. Chỉ include khi build cho STM32,
// vì file này cũng được compile chung cho ESP32 (ESP32 core đã tự có sẵn
// các kiểu FreeRTOS này qua Arduino.h rồi, không cần include thêm).
#if defined(ARDUINO_ARCH_STM32)
#include <STM32FreeRTOS.h>
#endif

// --------Cấu trúc dữ liệu chia sẻ ----------------
typedef struct {
  uint8_t  data[32];     // dữ liệu gửi cho drone (RX)
  uint32_t timestamp_us; // thời điểm gói tin được đóng gói (microsecond)
} RFData;

// --------khai báo toàn cục -----------------------
extern QueueHandle_t xQueueRF; // hàng đợi dữ liệu gửi cho drone (RX)

// --------khai báo hàm giao tiếp -----------------------
void RF_task(void *pvParameters); // hàm task gửi dữ liệu cho drone (RX)
bool initRF();                    // hàm khởi tạo module NRF24L01 bên TX

// --------khai báo hằng số -----------------------
// Pin mapping cho ESP32 DevKit V1 (VSPI mặc định) - dùng để BENCH TEST
// trước khi port sang ESP32-C3 thật. Khi port, nhớ đổi lại số GPIO vì
// ESP32-C3 không có đủ 30 chân như DevKit V1, cần chọn lại chân trống.
#define RF_CE_PIN   4   // GPIO4  - CE
#define RF_CSN_PIN  5   // GPIO5  - CSN
// SCK=GPIO18, MISO=GPIO19, MOSI=GPIO23 dùng VSPI mặc định, không cần define
// riêng vì thư viện SPI tự nhận đúng chân này khi gọi SPI.begin() không
// tham số trên ESP32 DevKit V1.

#endif