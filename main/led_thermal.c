/* Thermal warning LED: red breathing via WS2812 on GPIO48 using RMT. */

#include "led_thermal.h"

#include <math.h>
#include <stdbool.h>

#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_GPIO_NUM         48
#define LED_COUNT            1
#define RMT_RESOLUTION_HZ    10000000
#define FRAME_PERIOD_MS      20
#define BREATH_STEPS         100
#define MAX_BRIGHTNESS       80
#define LED_IDLE_POLL_MS     250

static const char *TAG = "led_thermal";
static TaskHandle_t s_led_task = NULL;
static bool s_warning_active = false;

static uint8_t s_led_pixels[LED_COUNT * 3];
static int s_breath_step = 0;
static int s_breath_direction = 1;

/* WS2812 bit encoding via RMT symbols */
static const rmt_symbol_word_t ws2812_zero = {
    .level0 = 1,  .duration0 = 0.3 * RMT_RESOLUTION_HZ / 1000000,
    .level1 = 0,  .duration1 = 0.9 * RMT_RESOLUTION_HZ / 1000000,
};
static const rmt_symbol_word_t ws2812_one = {
    .level0 = 1,  .duration0 = 0.9 * RMT_RESOLUTION_HZ / 1000000,
    .level1 = 0,  .duration1 = 0.3 * RMT_RESOLUTION_HZ / 1000000,
};
static const rmt_symbol_word_t ws2812_reset = {
    .level0 = 0,  .duration0 = RMT_RESOLUTION_HZ / 1000000 * 50 / 2,
    .level1 = 0,  .duration1 = RMT_RESOLUTION_HZ / 1000000 * 50 / 2,
};

static size_t ws2812_encoder_callback(const void *data,
                                      size_t data_size,
                                      size_t symbols_written,
                                      size_t symbols_free,
                                      rmt_symbol_word_t *symbols,
                                      bool *done,
                                      void *arg)
{
    (void)arg;

    if (symbols_free < 8) {
        return 0;
    }

    size_t data_pos = symbols_written / 8;
    const uint8_t *bytes = (const uint8_t *)data;
    if (data_pos < data_size) {
        size_t symbol_pos = 0;
        for (uint8_t bit = 0x80; bit != 0; bit >>= 1) {
            symbols[symbol_pos++] = (bytes[data_pos] & bit) ? ws2812_one : ws2812_zero;
        }
        return symbol_pos;
    }

    symbols[0] = ws2812_reset;
    *done = true;
    return 1;
}

static void set_pixel_grb(uint8_t red, uint8_t green, uint8_t blue)
{
    s_led_pixels[0] = green;
    s_led_pixels[1] = red;
    s_led_pixels[2] = blue;
}

static void show_pixel(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder)
{
    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    rmt_transmit(channel, encoder, s_led_pixels, sizeof(s_led_pixels), &tx_config);
    rmt_tx_wait_all_done(channel, portMAX_DELAY);
}

static uint8_t breath_brightness(void)
{
    float phase = (float)s_breath_step / BREATH_STEPS;
    uint8_t brightness = (uint8_t)((1.0f - cosf(phase * (float)M_PI)) * 0.5f * MAX_BRIGHTNESS);

    s_breath_step += s_breath_direction;
    if (s_breath_step >= BREATH_STEPS) {
        s_breath_step = BREATH_STEPS;
        s_breath_direction = -1;
    } else if (s_breath_step <= 0) {
        s_breath_step = 0;
        s_breath_direction = 1;
    }
    return brightness;
}

static void led_thermal_task(void *arg)
{
    (void)arg;

    rmt_channel_handle_t channel = NULL;
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_GPIO_NUM,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    rmt_encoder_handle_t encoder = NULL;
    rmt_simple_encoder_config_t enc_cfg = {
        .callback = ws2812_encoder_callback,
    };

    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &channel));
    ESP_ERROR_CHECK(rmt_new_simple_encoder(&enc_cfg, &encoder));
    ESP_ERROR_CHECK(rmt_enable(channel));

    /* Turn LED off initially */
    set_pixel_grb(0, 0, 0);
    show_pixel(channel, encoder);

    while (true) {
        if (s_warning_active) {
            uint8_t b = breath_brightness();
            set_pixel_grb(b, 0, 0);
            show_pixel(channel, encoder);
            vTaskDelay(pdMS_TO_TICKS(FRAME_PERIOD_MS));
        } else {
            /* Turn off LED and slow-poll when not warning */
            set_pixel_grb(0, 0, 0);
            show_pixel(channel, encoder);
            s_breath_step = 0;
            s_breath_direction = 1;
            vTaskDelay(pdMS_TO_TICKS(LED_IDLE_POLL_MS));
        }
    }
}

esp_err_t led_thermal_init(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(led_thermal_task,
                                            "led_thermal",
                                            4096,
                                            NULL,
                                            3,
                                            &s_led_task,
                                            1);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void led_thermal_set_warning(bool warning)
{
    s_warning_active = warning;
}
