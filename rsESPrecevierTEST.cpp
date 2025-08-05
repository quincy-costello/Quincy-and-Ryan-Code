//This is a testing receiver that just reads a signal and outputs it if it starts with a high signal(0). This one uses LEDC and the hardware timer.
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_system.h"
#include "M5GFX.h"

#define IR_RX_GPIO GPIO_NUM_36
#define BAUD_RATE 2400
#define BIT_DURATION_US (1000000 / BAUD_RATE) // ~416µs

M5GFX display;

volatile bool receiving = false;
volatile bool byteReady = false;
volatile int bitIndex = -1;
volatile uint8_t currentByte = 0;
volatile uint8_t receivedByte = 0;

gptimer_handle_t gptimer = NULL;

// ----------------------
// Timer ISR for sampling
// ----------------------
static bool IRAM_ATTR on_bit_timer(gptimer_handle_t,
                                   const gptimer_alarm_event_data_t*,
                                   void*) {
    bool level = gpio_get_level(IR_RX_GPIO);

    if (!receiving) {
        if (level == 0) { // start bit detected
            receiving = true;
            bitIndex = 0;
            currentByte = 0;
        }
        return false;
    }

    // Capture data bits
    if (bitIndex >= 0 && bitIndex < 8) {
        currentByte |= (level ? 1 : 0) << bitIndex;
    }

    bitIndex++;

    // Stop bit reached
    if (bitIndex > 8) {
        receiving = false;
        bitIndex = -1;

        // Store single byte result
        receivedByte = currentByte;
        byteReady = true;

        currentByte = 0;
    }

    return false;
}

// ----------------------
// Setup GPTimer
// ----------------------
void setup_gptimer() {
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000 // 1 MHz (1µs per tick)
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = on_bit_timer
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = BIT_DURATION_US,
        .reload_count = 0,
        .flags = { .auto_reload_on_alarm = true }
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
}

// ----------------------
// Print byte on LCD
// ----------------------
void printByteDebug(uint8_t val) {
    display.fillScreen(TFT_BLACK);
    display.setCursor(0, 0);
    display.println("RX Debug:");

    // Binary (LSB first)
    for (int b = 0; b < 8; b++) {
        display.print((val & (1 << b)) ? '1' : '0');
    }

    // Hex
    display.printf(" 0x%02X\n", val);
}

// ----------------------
// Main app
// ----------------------
extern "C" void app_main(void) {
    // LCD init
    display.begin();
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(1);
    display.fillScreen(TFT_BLACK);
    display.setCursor(0, 0);
    display.println("IR Single-Byte Debug RX");

    // Configure IR input
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << IR_RX_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Start GPTimer
    setup_gptimer();

    while (1) {
        if (byteReady) {
            byteReady = false;
            printByteDebug(receivedByte);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
