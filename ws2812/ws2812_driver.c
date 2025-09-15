#include "ws2812_driver.h"
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(ws2812_drv, CONFIG_LOG_DEFAULT_LEVEL);

int ws2812_init(struct ws2812_driver *drv, const char *label)
{
    drv->dev = DEVICE_DT_GET(DT_ALIAS(ledstrip));
    if (!drv->dev || !device_is_ready(drv->dev))
    {
        LOG_ERR("WS2812 device not ready");
        return -ENODEV;
    }
    drv->running = true;
    ws2812_clear(drv);
    LOG_INF("WS2812 driver init ok");
    return 0;
}

void ws2812_clear(struct ws2812_driver *drv)
{
    struct led_rgb black = {0, 0, 0};
    for (int i = 0; i < WS2812_NUM_LEDS; i++)
        drv->pixels[i] = black;
    led_strip_update_rgb(drv->dev, drv->pixels, WS2812_NUM_LEDS);
}

void ws2812_set_all(struct ws2812_driver *drv, struct led_rgb color)
{
    for (int i = 0; i < WS2812_NUM_LEDS; i++)
        drv->pixels[i] = color;
    led_strip_update_rgb(drv->dev, drv->pixels, WS2812_NUM_LEDS);
}

void ws2812_running_light(struct ws2812_driver *drv, struct led_rgb color, uint32_t delay_ms, uint32_t duration_ms)
{
    uint32_t end = k_uptime_get_32() + duration_ms;
    while (k_uptime_get_32() < end && drv->running)
    {
        ws2812_clear(drv);
        for (int i = 0; i < WS2812_NUM_LEDS && drv->running; i++)
        {
            drv->pixels[i] = color;
            led_strip_update_rgb(drv->dev, drv->pixels, WS2812_NUM_LEDS);
            k_msleep(delay_ms);
            drv->pixels[i] = (struct led_rgb){0, 0, 0};
        }
    }
    ws2812_clear(drv);
}

void ws2812_breathing(struct ws2812_driver *drv, struct led_rgb color, uint32_t duration_ms)
{
    uint32_t end = k_uptime_get_32() + duration_ms;
    uint8_t max_brightness = 255, step = 5;

    while (k_uptime_get_32() < end && drv->running)
    {
        for (uint8_t b = 0; b <= max_brightness && drv->running; b += step)
        {
            struct led_rgb c = {(color.r * b) / 255, (color.g * b) / 255, (color.b * b) / 255};
            ws2812_set_all(drv, c);
            k_msleep(20);
        }
        for (uint8_t b = max_brightness; b > 0 && drv->running; b -= step)
        {
            struct led_rgb c = {(color.r * b) / 255, (color.g * b) / 255, (color.b * b) / 255};
            ws2812_set_all(drv, c);
            k_msleep(20);
        }
    }
    ws2812_clear(drv);
}

void ws2812_rainbow(struct ws2812_driver *drv, uint32_t delay_ms, uint32_t duration_ms)
{
    uint32_t end = k_uptime_get_32() + duration_ms;
    uint8_t r = 255, g = 0, b = 0;

    while (k_uptime_get_32() < end && drv->running)
    {
        for (int i = 0; i < WS2812_NUM_LEDS; i++)
        {
            drv->pixels[i] = (struct led_rgb){r, g, b};
            if (r == 255 && g < 255 && b == 0)
                g += 5;
            else if (g == 255 && r > 0)
                r -= 5;
            else if (g == 255 && b < 255)
                b += 5;
            else if (b == 255 && g > 0)
                g -= 5;
            else if (b == 255 && r < 255)
                r += 5;
            else if (r == 255 && b > 0)
                b -= 5;
        }
        led_strip_update_rgb(drv->dev, drv->pixels, WS2812_NUM_LEDS);
        k_msleep(delay_ms);
    }
    ws2812_clear(drv);
}

void ws2812_sparkle(struct ws2812_driver *drv, struct led_rgb color, uint32_t delay_ms, uint32_t duration_ms)
{
    uint32_t end = k_uptime_get_32() + duration_ms;
    while (k_uptime_get_32() < end && drv->running)
    {
        ws2812_clear(drv);
        for (int i = 0; i < WS2812_NUM_LEDS / 3; i++)
        {
            int led = sys_rand32_get() % WS2812_NUM_LEDS;
            drv->pixels[led] = color;
        }
        led_strip_update_rgb(drv->dev, drv->pixels, WS2812_NUM_LEDS);
        k_msleep(delay_ms);
    }
    ws2812_clear(drv);
}