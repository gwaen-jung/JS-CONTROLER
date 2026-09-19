/* đây là môi trường của NRF24l01
ghi chú
gửi dữ liệu cho drone(TX)
gói dữ liệu dạng
joystick1 (trái) - VRX( throtle )
                 - VRY( yaw )
joystick2 (phải) - VRX( roll )
                 - VRY( pitch )
sơ đồ nối dây

 * NRF24L01     ESP32 DevKit V1 controler
 * ___________________________________________________
 * VCC  RED    |    3.3v   (KHONG duoc 5V)
 * GND  BROWN  |    GND
 * CSN  YELOW  |    GPIO5
 * CE   ORANGE |    GPIO4
 * SCK  GREEN  |    GPIO18  (VSPI mac dinh)
 * MOSI BLUE   |    GPIO23  (VSPI mac dinh)
 * MISO PURPLE |    GPIO19  (VSPI mac dinh)
 *
 * LUU Y: comment nay da sua lai cho dung ESP32 DevKit V1 - ban truoc do
 * bi dan nham ky hieu chan PA4/PA5/PA6/PA7 (ky hieu cua STM32) vao cot
 * ESP32, khong ton tai tren chip nay.
 */
#include <Arduino.h>
#include <SPI.h>
#include "RF_TX.h"
#include <RF24.h>

static RF24 radio(RF_CE_PIN, RF_CSN_PIN);

// Dinh nghia THAT cua xQueueRF (RF_TX.h chi co "extern" khai bao). Ben RX
// dinh nghia bien nay trong RF_RX.cpp, nhung file do bi loai khoi build
// cua env "esp32_Controler" (xem platformio.ini: -<RF_RX/>) nen ben TX
// PHAI tu dinh nghia rieng, neu khong linker se bao "undefined reference
// to xQueueRF" ngay khi build TX.
QueueHandle_t xQueueRF;

// RCCommand, RF_PIPE_ADDRESS, RF_PAYLOAD_SIZE gio lay tu RF_Protocol.h -
// dung CHUNG voi RF_RX.cpp ben STM32, khong con dinh nghia rieng o day nua
// de tranh lech dia chi pipe / kich thuoc payload giua 2 ben nhu truoc.

// GHI CHU QUAN TRONG - can xac nhan lai voi hardware inventory cua du an:
// theo tai lieu kien truc, joystick that su dung cam bien goc tu AS5600
// (doc qua I2C), KHONG phai bien tro analog thong thuong (VRX/VRY kieu
// analogRead). Ham doc joystick ben duoi dang TAM dung analogRead() de
// bench-test nhanh logic dong goi + gui RF trong luc chua noi AS5600 that.
// Truoc khi chuyen sang phan cung that, thay the ham nay bang doc I2C
// AS5600 (getAngle() hoac tuong duong) cho tung truc.
static int16_t readJoystickAxis_placeholder(uint8_t analogPin) {
    int raw = analogRead(analogPin);          // 0-4095 tren ESP32 (12-bit ADC)
    return (int16_t)map(raw, 0, 4095, -1000, 1000); // quy ve tam -1000..1000
}

// Chan analog TAM dung de bench test - doi lai theo wiring that neu tiep
// tuc dung analog joystick, hoac xoa het khi chuyen sang AS5600 that.
#define PIN_J1_VRX 34 // throttle
#define PIN_J1_VRY 35 // yaw
#define PIN_J2_VRX 32 // roll
#define PIN_J2_VRY 33 // pitch

// computeChecksum() gio lay tu RF_Protocol.h (dung chung voi RX de RX co
// the tinh lai va so sanh).

bool initRF() {
    if (!radio.begin()) {
        Serial.println(F("Module NRF24L01 (TX) khong hoat dong!"));
        return false;
    }
    // Cau hinh PHAI khop chinh xac voi ben RX (STM32) - channel, data rate,
    // payload size deu can giong het, khac 1 thong so la mat ket noi.
    radio.setChannel(76);
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_1MBPS);
    radio.setPayloadSize(RF_PAYLOAD_SIZE);
    radio.setAutoAck(true);
    // setRetries(delay, count): delay=5 -> (5+1)*250us = 1.5ms giua cac lan
    // gui lai, count=15 -> toi da 15 lan retry. Set tuong minh thay vi phu
    // thuoc default cua RF24 (mac dinh cung la 5,15 nhung khong ghi ro rang
    // ra thi nguoi doc code khong biet trade-off latency vs do tin cay dang
    // duoc ap dung nhu the nao, va de vo tinh bi doi khi update thu vien).
    radio.setRetries(5, 15);
    radio.openWritingPipe(RF_PIPE_ADDRESS);
    radio.stopListening(); // TX thi phai stopListening, khac voi RX dùng startListening()

    return true;
}

void RF_task(void *pvParameters) {
    (void) pvParameters;

    // DEBUG - checkpoint dau tien: neu dong nay khong bao gio hien ra thi
    // task chua bao gio duoc FreeRTOS scheduler chay (khac hoan toan voi
    // treo ben trong radio.write()) - can phan biet 2 truong hop nay.
    Serial.println(F("[RF_task] da vao task, chuan bi tao queue..."));

    if (xQueueRF == NULL) {
        xQueueRF = xQueueCreate(1, sizeof(RFData));
    }

    Serial.println(F("[RF_task] queue OK, vao vong lap chinh..."));

    for (;;) {
        RCCommand cmd;
        cmd.header   = RF_PACKET_HEADER;
        cmd.throttle = readJoystickAxis_placeholder(PIN_J1_VRX);
        cmd.yaw      = readJoystickAxis_placeholder(PIN_J1_VRY);
        cmd.roll     = readJoystickAxis_placeholder(PIN_J2_VRX);
        cmd.pitch    = readJoystickAxis_placeholder(PIN_J2_VRY);
        cmd.aux      = 0; // chua co nut bam, de mac dinh 0
        cmd.checksum = computeChecksum(cmd);

        // DEBUG - checkpoint ngay truoc radio.write(): neu "[RF_task] queue OK"
        // hien ra nhung khong bao gio thay dong nay o vong lap thu 2 tro di,
        // hoac khong bao gio thay dong "TX OK/FAIL" ben duoi -> chinh xac la
        // treo trong radio.write(), gan như chac chan do phan cung NRF24L01
        // (thieu tu loc nguon, day noi long CE/CSN, hoac mat nguon khi phat).
        bool ok = radio.write(&cmd, sizeof(cmd));

        // DEBUG - in ra de kiem tra khi bench test, xoa/giam tan suat khi
        // ghep vao firmware chinh thuc (in moi vong lap se ngap Serial).
        Serial.printf("TX %s: roll=%d pitch=%d yaw=%d throttle=%d\n",
                      ok ? "OK" : "FAIL",
                      cmd.roll, cmd.pitch, cmd.yaw, cmd.throttle);

        vTaskDelay(pdMS_TO_TICKS(20)); // 50Hz - du nhanh cho RC, chua can
                                        // toi da toc do RF24 co the dat duoc
    }
}