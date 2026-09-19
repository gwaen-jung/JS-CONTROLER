#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN    48
#define NUM_PIXELS 1

Adafruit_NeoPixel pixel(NUM_PIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  delay(1000);
  pixel.begin();
  pixel.setBrightness(50);
  Serial.println("RGB blink test bat dau...");
}

void loop() {
  pixel.setPixelColor(0, pixel.Color(255, 0, 0));
  pixel.show();
  delay(500);

  pixel.setPixelColor(0, pixel.Color(0, 0, 0));
  pixel.show();
  delay(500);

  Serial.println("blink");
}