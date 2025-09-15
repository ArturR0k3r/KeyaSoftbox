#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include <math.h>
#include "ksb_common.h"
#include "led_control.h"
#include "mesh_network.h"
#include "../ws2812/ws2812_driver.h"

LOG_MODULE_REGISTER(led_control, CONFIG_LOG_DEFAULT_LEVEL);

static struct led_control_context
{
    struct ws2812_driver ws_driver;
    enum ksb_led_pattern current_pattern;
    struct led_rgb current_color;
    uint32_t current_brightness;
    uint32_t current_speed;
    uint32_t frame_counter;
    bool running;
    struct k_thread led_thread;
    K_KERNEL_STACK_MEMBER(led_stack, 2048);
} led_ctx;

// Pattern implementations
static void pattern_off(struct led_rgb *leds, uint32_t frame)
{
    struct led_rgb black = {0, 0, 0};
    for (int i = 0; i < KSB_LED_COUNT; i++)
    {
        leds[i] = black;
    }
}

static void pattern_solid(struct led_rgb *leds, uint32_t frame)
{
    for (int i = 0; i < KSB_LED_COUNT; i++)
    {
        leds[i] = led_ctx.current_color;
    }
}

static void pattern_breathing(struct led_rgb *leds, uint32_t frame)
{
    // Sine wave breathing effect
    float breath = (sin((frame * led_ctx.current_speed) / 1000.0f) + 1.0f) / 2.0f;
    uint8_t brightness = (uint8_t)(breath * led_ctx.current_brightness);

    struct led_rgb color = {
        (led_ctx.current_color.r * brightness) / 255,
        (led_ctx.current_color.g * brightness) / 255,
        (led_ctx.current_color.b * brightness) / 255};

    for (int i = 0; i < KSB_LED_COUNT; i++)
    {
        leds[i] = color;
    }
}

static void pattern_running_light(struct led_rgb *leds, uint32_t frame)
{
    // Clear all LEDs
    struct led_rgb black = {0, 0, 0};
    for (int i = 0; i < KSB_LED_COUNT; i++)
    {
        leds[i] = black;
    }

    // Calculate position
    int pos = (frame * led_ctx.current_speed / 100) % KSB_LED_COUNT;
    leds[pos] = led_ctx.current_color;

    // Add trail
    int trail_pos = (pos - 1 + KSB_LED_COUNT) % KSB_LED_COUNT;
    leds[trail_pos] = (struct led_rgb){
        led_ctx.current_color.r / 3,
        led_ctx.current_color.g / 3,
        led_ctx.current_color.b / 3};
}

static void pattern_rainbow(struct led_rgb *leds, uint32_t frame)
{
    for (int i = 0; i < KSB_LED_COUNT; i++)
    {
        float hue = ((frame * led_ctx.current_speed / 10) + (i * 360 / KSB_LED_COUNT)) % 360;

        // Simple HSV to RGB conversion
        float c = (float)led_ctx.current_brightness / 255.0f;
        float x = c * (1 - fabs(fmod(hue / 60.0f, 2) - 1));
        float m = 0;

        float r, g, b;
        if (hue < 60)
        {
            r = c;
            g = x;
            b = 0;
        }
        else if (hue < 120)
        {
            r = x;
            g = c;
            b = 0;
        }
        else if (hue < 180)
        {
            r = 0;
            g = c;
            b = x;
        }
        else if (hue < 240)
        {
            r = 0;
            g = x;
            b = c;
        }
        else if (hue < 300)
        {
            r = x;
            g = 0;
            b = c;
        }
        else
        {
            r = c;
            g = 0;
            b = x;
        }

        leds[i] = (struct led_rgb){
            (uint8_t)((r + m) * 255),
            (uint8_t)((g + m) * 255),
            (uint8_t)((b + m) * 255)};
    }
}

static void pattern_sparkle(struct led_rgb *leds, uint32_t frame)
{
    // Start with dim background
    struct led_rgb dim_color = {
        led_ctx.current_color.r / 10,
        led_ctx.current_color.g / 10,
        led_ctx.current_color.b / 10};

    for (int i = 0; i < KSB_LED_COUNT; i++)
    {
        leds[i] = dim_color;
    }

    // Add random sparkles
    if ((frame % (200 - led_ctx.current_speed)) == 0)
    {
        int sparkle_pos = sys_rand32_get() % KSB_LED_COUNT;
        leds[sparkle_pos] = led_ctx.current_color;
    }
}

static void pattern_wave(struct led_rgb *leds, uint32_t frame)
{
    for (int i = 0; i < KSB_LED_COUNT; i++)
    {
        float wave = sin((frame * led_ctx.current_speed / 100.0f) + (i * 3.14159f / KSB_LED_COUNT));
        wave = (wave + 1.0f) / 2.0f; // Normalize to 0-1

        uint8_t brightness = (uint8_t)(wave * led_ctx.current_brightness);

        leds[i] = (struct led_rgb){
            (led_ctx.current_color.r * brightness) / 255,
            (led_ctx.current_color.g * brightness) / 255,
            (led_ctx.current_color.b * brightness) / 255};
    }
}

// LED control thread
static void led_control_thread(void *arg1, void *arg2, void *arg3)
{
    struct led_rgb leds[KSB_LED_COUNT];

    while (led_ctx.running)
    {
        // Apply brightness scaling
        uint8_t brightness_scale = led_ctx.current_brightness;

        // Generate pattern
        switch (led_ctx.current_pattern)
        {
        case KSB_PATTERN_OFF:
            pattern_off(leds, led_ctx.frame_counter);
            break;
        case KSB_PATTERN_SOLID:
            pattern_solid(leds, led_ctx.frame_counter);
            break;
        case KSB_PATTERN_BREATHING:
            pattern_breathing(leds, led_ctx.frame_counter);
            break;
        case KSB_PATTERN_RUNNING_LIGHT:
            pattern_running_light(leds, led_ctx.frame_counter);
            break;
        case KSB_PATTERN_RAINBOW:
            pattern_rainbow(leds, led_ctx.frame_counter);
            break;
        case KSB_PATTERN_SPARKLE:
            pattern_sparkle(leds, led_ctx.frame_counter);
            break;
        case KSB_PATTERN_WAVE:
            pattern_wave(leds, led_ctx.frame_counter);
            break;
        default:
            pattern_off(leds, led_ctx.frame_counter);
            break;
        }

        // Apply global brightness scaling (except for breathing which handles its own)
        if (led_ctx.current_pattern != KSB_PATTERN_BREATHING)
        {
            for (int i = 0; i < KSB_LED_COUNT; i++)
            {
                leds[i].r = (leds[i].r * brightness_scale) / 255;
                leds[i].g = (leds[i].g * brightness_scale) / 255;
                leds[i].b = (leds[i].b * brightness_scale) / 255;
            }
        }

        // Update physical LEDs
        for (int i = 0; i < KSB_LED_COUNT; i++)
        {
            led_ctx.ws_driver.pixels[i] = leds[i];
        }
        led_strip_update_rgb(led_ctx.ws_driver.dev, led_ctx.ws_driver.pixels, KSB_LED_COUNT);

        led_ctx.frame_counter++;
        k_msleep(KSB_LED_UPDATE_RATE_MS);
    }
}

int led_control_init(void)
{
    int ret;

    // Initialize WS2812 driver
    ret = ws2812_init(&led_ctx.ws_driver, "WS2812");
    if (ret != 0)
    {
        LOG_ERR("Failed to initialize WS2812 driver: %d", ret);
        return ret;
    }

    // Set default pattern
    led_ctx.current_pattern = KSB_PATTERN_OFF;
    led_ctx.current_color = (struct led_rgb){100, 100, 100};
    led_ctx.current_brightness = 128;
    led_ctx.current_speed = 100;
    led_ctx.frame_counter = 0;
    led_ctx.running = true;

    // Start LED control thread
    k_thread_create(&led_ctx.led_thread, led_ctx.led_stack,
                    K_KERNEL_STACK_SIZEOF(led_ctx.led_stack),
                    led_control_thread, NULL, NULL, NULL,
                    7, 0, K_NO_WAIT);
    k_thread_name_set(&led_ctx.led_thread, "led_ctrl");

    LOG_INF("LED control initialized");
    return 0;
}

void led_control_set_pattern(enum ksb_led_pattern pattern, struct led_rgb color,
                             uint8_t brightness, uint32_t speed)
{
    led_ctx.current_pattern = pattern;
    led_ctx.current_color = color;
    led_ctx.current_brightness = brightness;
    led_ctx.current_speed = speed;
    led_ctx.frame_counter = 0;

    LOG_INF("LED pattern set: %d, color: (%d,%d,%d), brightness: %d, speed: %d",
            pattern, color.r, color.g, color.b, brightness, speed);
}

void led_control_next_pattern(void)
{
    enum ksb_led_pattern next = (led_ctx.current_pattern + 1) % KSB_PATTERN_COUNT;

    // Skip OFF pattern when cycling
    if (next == KSB_PATTERN_OFF)
    {
        next = KSB_PATTERN_SOLID;
    }

    struct led_rgb colors[] = {
        {255, 0, 0},    // Red
        {0, 255, 0},    // Green
        {0, 0, 255},    // Blue
        {255, 255, 0},  // Yellow
        {255, 0, 255},  // Magenta
        {0, 255, 255},  // Cyan
        {255, 255, 255} // White
    };

    struct led_rgb color = colors[sys_rand32_get() % ARRAY_SIZE(colors)];

    led_control_set_pattern(next, color, 128, 100);

    // Broadcast to mesh if connected
    if (mesh_network_is_connected())
    {
        struct ksb_led_command cmd = {
            .pattern = next,
            .color = color,
            .brightness = 128,
            .speed = 100,
            .frame = 0};
        mesh_broadcast_led_command(&cmd);
    }
}

enum ksb_led_pattern led_control_get_current_pattern(void)
{
    return led_ctx.current_pattern;
}