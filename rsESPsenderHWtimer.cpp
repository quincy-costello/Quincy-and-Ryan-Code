//This code is for the ESP-IDF framework.

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "M5GFX.h" // M5Stack LCD
#include <string.h>

#define IR_TX_GPIO GPIO_NUM_26
#define BUTTON_A_GPIO GPIO_NUM_39

#define BAUD_RATE 2400
#define BIT_DURATION_US (1000000 / BAUD_RATE) // ~416µs
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_TIMER   LEDC_TIMER_0
#define LEDC_FREQ    38000
#define LEDC_RES     LEDC_TIMER_8_BIT

// --- Globals ---
M5GFX display;
uint8_t packet[8]; // 2-byte preamble + 6-byte MAC

volatile bool transmitting = false;
volatile int bitIndex = 0;
volatile int byteIndex = 0;
volatile uint8_t currentByte = 0;

gptimer_handle_t bit_timer = NULL;

// --- GPTimer ISR ---
static bool IRAM_ATTR on_bit_timer(gptimer_handle_t timer,
                                   const gptimer_alarm_event_data_t *edata,
                                   void *user_ctx)
{
    if (!transmitting) return false;

    bool bit;
    if (bitIndex == 0) { // Start bit
        bit = 0;
    } else if (bitIndex >= 1 && bitIndex <= 8) { // Data bits (LSB first)
        bit = (currentByte >> (bitIndex - 1)) & 0x01;
    } else { // Stop bit
        bit = 1;
    }

    if (bit == 0) {
        // Turn ON LEDC modulation
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL, 128);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL);
    } else {
        // Turn OFF LEDC modulation
        ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL, 0);
    }

    bitIndex++;
    if (bitIndex > 9) { // Next byte
        bitIndex = 0;
        byteIndex++;
        if (byteIndex < sizeof(packet)) {
            currentByte = packet[byteIndex];
        } else {
            // Done sending
            transmitting = false;
            ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL, 0);
        }
    }

    return true; // Keep timer running
}

// --- LEDC Setup ---
void setup_ledc()
{
    ledc_timer_config_t ledc_timer_conf = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer_conf);

    ledc_channel_config_t ledc_channel_conf = {
        .gpio_num = IR_TX_GPIO,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel_conf);

    ledc_stop(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL, 0);
}

// --- GPTimer Setup ---
void setup_gptimer()
{
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1us per tick
        .intr_priority = 0,
        .flags = { .intr_shared = false }
    };
    gptimer_new_timer(&timer_config, &bit_timer);

    gptimer_event_callbacks_t cbs = {
        .on_alarm = on_bit_timer
    };
    gptimer_register_event_callbacks(bit_timer, &cbs, NULL);

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = BIT_DURATION_US,
        .reload_count = 0,
        .flags = { .auto_reload_on_alarm = true }
    };
    gptimer_set_alarm_action(bit_timer, &alarm_config);

    gptimer_enable(bit_timer);
}

// --- Transmission ---
void start_transmission()
{
    if (transmitting) return;

    transmitting = true;
    bitIndex = 0;
    byteIndex = 0;
    currentByte = packet[0];

    gptimer_start(bit_timer);
}

// --- Main ---
extern "C" void app_main(void)
{
    // Init display
    display.begin();
    display.setTextSize(2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.fillScreen(TFT_BLACK);
    display.setCursor(0, 0);
    display.println("ESP-IDF IR Sender");

    // Init Button A
    gpio_config_t btn_conf = {};
    btn_conf.pin_bit_mask = 1ULL << BUTTON_A_GPIO;
    btn_conf.mode = GPIO_MODE_INPUT;
    btn_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&btn_conf);

    // Get MAC and build packet
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    packet[0] = 0x5A; // 'Z'
    packet[1] = 0x54; // 'T'
    memcpy(&packet[2], mac, 6);

    // Setup LEDC & Timer
    setup_ledc();
    setup_gptimer();

    while (1) {
        if (gpio_get_level(BUTTON_A_GPIO) == 0) {
            // Show MAC being sent
            display.fillScreen(TFT_BLACK);
            display.setCursor(0, 0);
            display.println("Sending Packet:");
            display.printf("ZT %02X:%02X:%02X:%02X:%02X:%02X\n",
                           mac[0], mac[1], mac[2],
                           mac[3], mac[4], mac[5]);

            // Start transmission
            start_transmission();

            // Debounce: wait for release
            while (gpio_get_level(BUTTON_A_GPIO) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
