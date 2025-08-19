#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "../ws2812/ws2812_driver.h"

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

#define NUM_PATTERNS 5

static struct ws2812_driver wsdrv;
static volatile bool pattern_running = true;
static int current_pattern = 0;

static void pattern_thread(void)
{
    struct led_rgb red = {255, 0, 0};
    struct led_rgb green = {0, 255, 0};
    struct led_rgb blue = {0, 0, 255};
    struct led_rgb white = {255, 255, 255};

    while (true)
    {
        if (!pattern_running)
        {
            k_msleep(100);
            continue;
        }

        switch (current_pattern)
        {
        case 0:
            LOG_INF("Pattern 0: Solid Red");
            ws2812_set_all(&wsdrv, red);
            k_msleep(2000);
            break;
        case 1:
            LOG_INF("Pattern 1: Running Green Light");
            ws2812_running_light(&wsdrv, green, 100, 5000);
            break;
        case 2:
            LOG_INF("Pattern 2: Breathing Blue");
            ws2812_breathing(&wsdrv, blue, 5000);
            break;
        case 3:
            LOG_INF("Pattern 3: Rainbow");
            ws2812_rainbow(&wsdrv, 50, 5000);
            break;
        case 4:
            LOG_INF("Pattern 4: White Sparkle");
            ws2812_sparkle(&wsdrv, white, 100, 5000);
            break;
        default:
            current_pattern = 0;
            break;
        }
    }
}

K_THREAD_DEFINE(pattern_tid, 2048, pattern_thread, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    LOG_INF("WS2812B LED Strip Demo Starting...");

    if (ws2812_init(&wsdrv, "WS2812") != 0)
    {
        LOG_ERR("Failed to init WS2812 driver");
        return -ENODEV;
    }

    LOG_INF("LED strip ready - %d LEDs configured", WS2812_NUM_LEDS);

    ws2812_set_all(&wsdrv, (struct led_rgb){50, 0, 0});
    k_msleep(500);
    ws2812_clear(&wsdrv);

    LOG_INF("Init complete, starting pattern thread");
    return 0;
}
