#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"

// GPIO assignment
static const char* TAG = "turbo_ledstrip";
#define LED_STRIP_BLINK_GPIO  17
// #define LED_STRIP_BLINK_GPIO  4
// Numbers of the LED in the strip
#define LED_STRIP_LED_NUMBERS 300
// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)
led_strip_handle_t configure_led(void)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_BLINK_GPIO,   // The GPIO that connected to the LED strip's data line
        .max_leds = LED_STRIP_LED_NUMBERS,        // The number of LEDs in the strip,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Pixel format of your LED strip
        .led_model = LED_MODEL_WS2812,            // LED strip model
        .flags.invert_out = false,                // whether to invert the output signal
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = LED_STRIP_RMT_RES_HZ, // RMT counter clock frequency
        .flags.with_dma = false,               // DMA feature is available on ESP target like ESP32-S3
    };

    // LED Strip object handle
    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");
    return led_strip;
}

void ledstrip_task(void *pvParameters)
{
    QueueHandle_t* queue_handle = (QueueHandle_t*) pvParameters;
    led_strip_handle_t led_strip = configure_led();

    ESP_LOGI(TAG, "Start blinking LED strip");
    uint8_t buffer[3];
    size_t current_pixel = 0;
    while (true) {
        if (!xQueueReceive(*queue_handle, &buffer, 5)) {
            continue;
        }

        const uint8_t r = buffer[0];
        const uint8_t g = buffer[1];
        const uint8_t b = buffer[2];
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, current_pixel++, r, g, b));
        if (current_pixel == LED_STRIP_LED_NUMBERS) {
            current_pixel = 0;
            ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        }
    }
}
