#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"
#include <math.h>

#include "conf.h"

// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)

// 定义 TAG 用于日志输出
static const char *TAG = "led_task";

// 提前声明 configure_led 函数
led_strip_handle_t configure_led(void);

// 将HSV颜色转换为RGB颜色
void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b) {
    int i;
    float f, p, q, t;

    if (s == 0) {
        // 灰色
        *r = *g = *b = (uint8_t)(v * 255);
        return;
    }

    h /= 60;  // 将色相转换为0-6的范围
    i = (int)floor(h);
    f = h - i;
    p = v * (1 - s);
    q = v * (1 - s * f);
    t = v * (1 - s * (1 - f));

    switch (i) {
        case 0:
            *r = (uint8_t)(v * 255);
            *g = (uint8_t)(t * 255);
            *b = (uint8_t)(p * 255);
            break;
        case 1:
            *r = (uint8_t)(q * 255);
            *g = (uint8_t)(v * 255);
            *b = (uint8_t)(p * 255);
            break;
        case 2:
            *r = (uint8_t)(p * 255);
            *g = (uint8_t)(v * 255);
            *b = (uint8_t)(t * 255);
            break;
        case 3:
            *r = (uint8_t)(p * 255);
            *g = (uint8_t)(q * 255);
            *b = (uint8_t)(v * 255);
            break;
        case 4:
            *r = (uint8_t)(t * 255);
            *g = (uint8_t)(p * 255);
            *b = (uint8_t)(v * 255);
            break;
        default:
            *r = (uint8_t)(v * 255);
            *g = (uint8_t)(p * 255);
            *b = (uint8_t)(q * 255);
            break;
    }
}

led_strip_handle_t configure_led(void)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN, // The GPIO that connected to the LED strip's data line
        .max_leds = LED_STRIP_LED_COUNT,      // The number of LEDs in the strip,
        .led_model = LED_MODEL_WS2812,        // LED strip model
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // The color order of the strip: GRB
        .flags = {
            .invert_out = false, // don't invert the output signal
        }
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = LED_STRIP_RMT_RES_HZ, // RMT counter clock frequency
        .mem_block_symbols = 64,               // the memory size of each RMT channel, in words (4 bytes)
        .flags = {
            .with_dma = false, // DMA feature is available on chips like ESP32-S3/P4
        }
    };

    // LED Strip object handle
    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");
    return led_strip;
}

void led_app_main(void *param)
{
    led_strip_handle_t led_strip = configure_led();

    ESP_LOGI(TAG, "Start smooth color transition on LED strip");

    float hue = 0.0;  // 色相值 (0-360)
    float saturation = 1.0;  // 饱和度 (0-1)
    float value = 1.0;  // 亮度 (0-1)

    while (1) {
        // 将HSV转换为RGB
        uint8_t r, g, b;
        hsv_to_rgb(hue, saturation, value, &r, &g, &b);

        // 设置LED颜色
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, r, g, b));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));

        // 增加色相值，实现颜色过渡
        hue += 1.0;
        if (hue >= 360.0) {
            hue = 0.0;
        }

        // 延时一段时间，控制颜色变化速度
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}