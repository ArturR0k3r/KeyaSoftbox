#pragma once
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/random/random.h>

#define WS2812_NUM_LEDS 8

struct ws2812_driver
{
    const struct device *dev;
    struct led_rgb pixels[WS2812_NUM_LEDS];
    volatile bool running;
};

/* Init */
int ws2812_init(struct ws2812_driver *drv, const char *label);

/* Utils */
void ws2812_clear(struct ws2812_driver *drv);
void ws2812_set_all(struct ws2812_driver *drv, struct led_rgb color);

/* Effects */
void ws2812_running_light(struct ws2812_driver *drv, struct led_rgb color, uint32_t delay_ms, uint32_t duration_ms);
void ws2812_breathing(struct ws2812_driver *drv, struct led_rgb color, uint32_t duration_ms);
void ws2812_rainbow(struct ws2812_driver *drv, uint32_t delay_ms, uint32_t duration_ms);
void ws2812_sparkle(struct ws2812_driver *drv, struct led_rgb color, uint32_t delay_ms, uint32_t duration_ms);