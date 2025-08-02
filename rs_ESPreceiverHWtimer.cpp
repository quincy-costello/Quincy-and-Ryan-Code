#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "M5GFX.h"

#define IR_RX_GPIO GPIO_NUM_36
#define BAUD_RATE 2400
#define BIT_DURATION_US (1000000 / BAUD_RATE)

#define SYNC1 0x5A // 'Z'
#define SYNC2 0x54 // 'T'

M5GFX display;

volatile bool receiving = false;
volatile int bitIndex = -1;
volatile uint8_t currentByte = 0;

volatile int syncState = 0;
volatile uint8_t mac[6];
volatile int macIndex = 0;
volatile bool macReady = false;

uint8_t mac_self[6];

// ----------------------
// Timer ISR for sampling
// ----------------------
static bool IRAM_ATTR on_bit_timer(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*) {
    bool level = gpio_get_level(IR_RX_GPIO);

    if (!receiving) {
        if (level == 0) {
            receiving = true;
            bitIndex = 0;
            currentByte = 0;
        }
        return false;
    }

    if (bitIndex >= 0 && bitIndex < 8) {
        currentByte |= (level ? 1 : 0) << bitIndex;
    }

    bitIndex++;

    if (bitIndex > 8) { // stop bit
        receiving = false;
        bitIndex = -1;

        if (syncState == 0 && currentByte == SYNC1) {
            syncState = 1;
        }
        else if (syncState == 1 && currentByte == SYNC2) {
            syncState = 2;
            macIndex = 0;
        }
        else if (syncState == 2) {
            mac[macIndex++] = currentByte;
            if (macIndex >= 6) {
                macReady = true;
                syncState = 0;
            }
        }
        else {
            syncState = 0;
        }

        currentByte = 0;
    }
    return false;
}

// ----------------------
// Compare received MAC to own
// ----------------------
bool isOwnMAC() {
    for (int i = 0; i < 6; i++) {
        if (mac[i] != mac_self[i]) return false;
    }
    return true;
}

// ----------------------
// Timer setup
// ----------------------
void setup_gptimer() {
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000 // 1 MHz
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
// Main app
// ----------------------
extern "C" void app_main(void) {
    // LCD
    display.begin();
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(2);
    display.fillScreen(TFT_BLACK);
    display.setCursor(0, 0);
    display.println("IR Receiver (ZT + MAC)");

    // Configure IR input
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << IR_RX_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Read our MAC
    esp_read_mac(mac_self, ESP_MAC_WIFI_STA);

    // Start sampling
    setup_gptimer();

    // Main loop
    while (1) {
        if (macReady) {
            macReady = false;

            if (isOwnMAC()) {
                printf("Ignored: Own MAC received.\n");
                continue;
            }

            printf("Received MAC: ");
            display.fillScreen(TFT_BLACK);
            display.setCursor(0, 0);
            display.print("Recv MAC: ");

            for (int i = 0; i < 6; i++) {
                printf("%02X", mac[i]);
                display.printf("%02X", mac[i]);
                if (i < 5) {
                    printf(":");
                    display.print(":");
                }
            }
            printf("\n");
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
