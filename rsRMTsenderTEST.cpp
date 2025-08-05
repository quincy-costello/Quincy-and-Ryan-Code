//This is a test IR sender for the ESP-IDF framekwork.
//It uses RMT to handle the signal sending.
//It sends a basic "Z" for testing.
#include <stdio.h>
#include <string.h>
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "M5GFX.h"

#define IR_TX_GPIO GPIO_NUM_26
#define BUTTON_A_GPIO GPIO_NUM_39

#define BAUD_RATE 2400
#define BIT_US (1000000 / BAUD_RATE) // ~416 µs per bit
#define CARRIER_FREQ 38000 // 38 kHz

#define TX_BYTE 'Z' // 0x5A

M5GFX display;

// RMT handles
static rmt_channel_handle_t rmt_chan = NULL;
static rmt_encoder_handle_t copy_encoder = NULL;

// Persistent symbol buffer
static rmt_symbol_word_t symbols[16];

// ----------------------
// Build UART-style byte for IR
// ----------------------
static size_t build_uart_byte(uint8_t byte) {
    size_t idx = 0;

    auto set_mark = [&](size_t &i) {
        // Logic 0 = mark (carrier ON for whole bit)
        symbols[i].level0 = 1; // High
        symbols[i].duration0 = BIT_US; // Entire bit time
        symbols[i].level1 = 0; // Low
        symbols[i].duration1 = 1; // Minimal gap (non-zero)
        i++;
    };

    auto set_space = [&](size_t &i) {
        // Logic 1 = space (carrier OFF for whole bit)
        symbols[i].level0 = 0; // Low
        symbols[i].duration0 = BIT_US; // Entire bit time
        symbols[i].level1 = 0;
        symbols[i].duration1 = 1; // Minimal gap (non-zero)
        i++;
    };

    // Start bit = logic 0
    set_mark(idx);

    // Data bits (LSB first)
    for (int b = 0; b < 8; b++) {
        bool bit = (byte >> b) & 0x01;
        if (bit == 0) {
            set_mark(idx);
        } else {
            set_space(idx);
        }
    }

    // Stop bit = logic 1
    set_space(idx);

    return idx;
}

// ----------------------
// Setup RMT
// ----------------------
static void setup_rmt() {
    rmt_tx_channel_config_t tx_chan_cfg = {
        .gpio_num = IR_TX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1 tick = 1 µs
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .intr_priority = 0,
        .flags = {0}
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_cfg, &rmt_chan));

    // Apply 38 kHz carrier for marks
    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = CARRIER_FREQ,
        .duty_cycle = 0.5,
        .flags = { .polarity_active_low = 0 }
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(rmt_chan, &carrier_cfg));

    // Copy encoder for raw symbol sending
    rmt_copy_encoder_config_t copy_cfg = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_cfg, &copy_encoder));

    ESP_ERROR_CHECK(rmt_enable(rmt_chan));
}

// ----------------------
// Send a byte
// ----------------------
static void send_byte(uint8_t byte) {
    size_t count = build_uart_byte(byte);

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
        .flags = {0}
    };

    ESP_ERROR_CHECK(rmt_transmit(
        rmt_chan, copy_encoder,
        symbols, count * sizeof(rmt_symbol_word_t),
        &tx_cfg
    ));

    ESP_ERROR_CHECK(rmt_tx_wait_all_done(rmt_chan, -1));
}

// ----------------------
// Debug print
// ----------------------
static void print_tx_debug(uint8_t val) {
    display.fillScreen(TFT_BLACK);
    display.setCursor(0, 0);
    display.println("TX Debug:");
    for (int b = 0; b < 8; b++) {
        display.print((val & (1 << b)) ? '1' : '0');
    }
    display.printf(" 0x%02X\n", val);
}

// ----------------------
// Main
// ----------------------
extern "C" void app_main(void) {
    // Init LCD
    display.begin();
    display.setTextSize(1);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.fillScreen(TFT_BLACK);
    display.setCursor(0, 0);
    display.println("RMT Single-Byte Sender");

    // Init button
    gpio_config_t btn_cfg = {};
    btn_cfg.pin_bit_mask = 1ULL << BUTTON_A_GPIO;
    btn_cfg.mode = GPIO_MODE_INPUT;
    btn_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&btn_cfg);

    // Setup RMT
    setup_rmt();

    uint8_t byte_to_send = TX_BYTE;

    while (1) {
        if (gpio_get_level(BUTTON_A_GPIO) == 0) {
            print_tx_debug(byte_to_send);
            send_byte(byte_to_send);

            // Debounce
            while (gpio_get_level(BUTTON_A_GPIO) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
