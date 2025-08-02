#include <M5Stack.h>
#include <FastLED.h>
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_system.h"

#define MODULATED_IR_PIN GPIO_NUM_26
#define LED_PIN 15
#define LED_COUNT 10

#define BAUD_RATE 2400
#define BIT_DURATION_US (1000000 / BAUD_RATE)

#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_TIMER   LEDC_TIMER_0
#define LEDC_FREQ    38000
#define LEDC_RES     LEDC_TIMER_8_BIT

CRGB leds[LED_COUNT];
uint8_t mac[6];
uint8_t packet[8];  // 2-byte preamble + 6-byte MAC

// Transmission state
volatile bool transmitting = false;
volatile int bitIndex = 0;
volatile int byteIndex = 0;
volatile uint8_t currentByte = 0;

// Timer
hw_timer_t* bitTimer = NULL;

void IRAM_ATTR onBitTimer() {
  if (!transmitting) return;

  bool bit;
  if (bitIndex == 0) bit = 0; // Start bit
  else if (bitIndex >= 1 && bitIndex <= 8)
    bit = (currentByte >> (bitIndex - 1)) & 0x01;
  else bit = 1; // Stop bit

  // Modulate for 0; silence for 1
  if (bit == 0) {
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL, 128);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL);
  } else {
    ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL, 0);
  }

  bitIndex++;
  if (bitIndex > 9) {
    bitIndex = 0;
    byteIndex++;
    if (byteIndex < 8) {
      currentByte = packet[byteIndex];
    } else {
      transmitting = false;
      ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL, 0);
      timerAlarmDisable(bitTimer);
    }
  }
}

void setupLEDC() {
  ledc_timer_config_t timer_conf = {
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .timer_num = LEDC_TIMER,
    .duty_resolution = LEDC_RES,
    .freq_hz = LEDC_FREQ,
    .clk_cfg = LEDC_AUTO_CLK
  };
  ledc_timer_config(&timer_conf);

  ledc_channel_config_t channel_conf = {
    .channel = LEDC_CHANNEL,
    .duty = 0,
    .gpio_num = MODULATED_IR_PIN,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .hpoint = 0,
    .timer_sel = LEDC_TIMER
  };
  ledc_channel_config(&channel_conf);
  ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL, 0);
}

void setupTimer() {
  bitTimer = timerBegin(0, 80, true); // 1 MHz
  timerAttachInterrupt(bitTimer, &onBitTimer, true);
  timerAlarmWrite(bitTimer, BIT_DURATION_US, true);
  timerAlarmDisable(bitTimer);
}

void startTransmission() {
  transmitting = true;
  byteIndex = 0;
  bitIndex = 0;
  currentByte = packet[0];
  timerAlarmEnable(bitTimer);
}

void flashRed() {
  for (int i = 0; i < LED_COUNT; i++) leds[i] = CRGB::Red;
  FastLED.show();
  delay(150);
  FastLED.clear();
  FastLED.show();
}

void printMAC() {
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.print("Sent MAC: ");
  for (int i = 0; i < 6; i++) {
    M5.Lcd.printf("%02X", mac[i]);
    if (i < 5) M5.Lcd.print(":");
  }
}

void setup() {
  M5.begin();
  Serial.begin(115200);
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.println("IR Sender (ZT + MAC)");

  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
  FastLED.clear(); FastLED.show();

  setupLEDC();
  setupTimer();

  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  packet[0] = 0x5A;  // 'Z'
  packet[1] = 0x54;  // 'T'
  memcpy(&packet[2], mac, 6);
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed() && !transmitting) {
    Serial.print("Sending MAC: ");
    for (int i = 0; i < 6; i++) {
      Serial.printf("%02X", mac[i]);
      if (i < 5) Serial.print(":");
    }
    Serial.println();

    printMAC();
    startTransmission();
    flashRed();
  }
}
