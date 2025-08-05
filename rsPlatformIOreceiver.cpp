//This code is a receiver for the PlatformIO framework.
//It uses LEDC and the hardware timer.
//It filters out self signals using the MAC address and looks for the "ZT" preamble.
#include <M5Stack.h>
#include <FastLED.h>
#include "driver/gpio.h"
#include "driver/timer.h"
#include "esp_system.h"

#define IR_RECEIVE_PIN GPIO_NUM_36
#define LED_PIN 15
#define LED_COUNT 10

#define BAUD_RATE 2400
#define BIT_DURATION_US (1000000 / BAUD_RATE)

CRGB leds[LED_COUNT];

const uint8_t SYNC1 = 0x5A;
const uint8_t SYNC2 = 0x54;

volatile bool receiving = false;
volatile int bitIndex = -1;
volatile uint8_t currentByte = 0;

volatile int syncState = 0;
volatile uint8_t mac[6];
volatile int macIndex = 0;
volatile bool macReady = false;

uint8_t mac_self[6];

hw_timer_t* bitTimer = NULL;

void IRAM_ATTR onSampleTimer() {
  bool level = gpio_get_level(IR_RECEIVE_PIN);

  if (!receiving) {
    if (level == 0) {
      receiving = true;
      bitIndex = 0;
      currentByte = 0;
    }
    return;
  }

  if (bitIndex >= 0 && bitIndex < 8) {
    currentByte |= (level ? 1 : 0) << bitIndex;
  }

  bitIndex++;

  if (bitIndex > 8) {
    receiving = false;
    bitIndex = -1;

    if (syncState == 0 && currentByte == SYNC1) {
      syncState = 1;
    } else if (syncState == 1 && currentByte == SYNC2) {
      syncState = 2;
      macIndex = 0;
    } else if (syncState == 2) {
      mac[macIndex++] = currentByte;
      if (macIndex >= 6) {
        macReady = true;
        syncState = 0;
      }
    } else {
      syncState = 0;
    }

    currentByte = 0;
  }
}

bool isOwnMAC() {
  for (int i = 0; i < 6; i++) {
    if (mac[i] != mac_self[i]) return false;
  }
  return true;
}

void flashGreen() {
  for (int i = 0; i < LED_COUNT; i++) leds[i] = CRGB::Green;
  FastLED.show();
  delay(150);
  FastLED.clear(); FastLED.show();
}

void printMAC() {
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.print("Recv MAC: ");
  for (int i = 0; i < 6; i++) {
    M5.Lcd.printf("%02X", mac[i]);
    if (i < 5) M5.Lcd.print(":");
  }
}

void setupTimer() {
  bitTimer = timerBegin(1, 80, true);
  timerAttachInterrupt(bitTimer, &onSampleTimer, true);
  timerAlarmWrite(bitTimer, BIT_DURATION_US, true);
  timerAlarmEnable(bitTimer);
}

void setup() {
  M5.begin();
  Serial.begin(115200);
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("IR Receiver (ZT + MAC)");

  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
  FastLED.clear(); FastLED.show();

  gpio_pad_select_gpio(IR_RECEIVE_PIN);
  gpio_set_direction(IR_RECEIVE_PIN, GPIO_MODE_INPUT);

  esp_read_mac(mac_self, ESP_MAC_WIFI_STA);
  setupTimer();
}

void loop() {
  if (macReady) {
    macReady = false;

    if (isOwnMAC()) {
      Serial.println("Ignored: Own MAC received.");
      return;
    }

    Serial.print("Received MAC: ");
    for (int i = 0; i < 6; i++) {
      Serial.printf("%02X", mac[i]);
      if (i < 5) Serial.print(":");
    }
    Serial.println();

    printMAC();
    flashGreen();
  }
}
