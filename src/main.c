#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

/* Use built-in LED strip driver */
#define LED_STRIP_NODE DT_PATH(WS2812)
#define NUM_LEDS 8

static struct led_rgb pixels[NUM_LEDS];
struct device *led_strip;
static volatile bool pattern_running = true;
static int current_pattern = 0;

#if HAS_BUTTON
static struct gpio_callback button_callback;
#endif

static void clear_strip(void)
{
    struct led_rgb black = {.r = 0, .g = 0, .b = 0};
    for (int i = 0; i < NUM_LEDS; i++)
    {
        pixels[i] = black;
    }
    led_strip_update_rgb(led_strip, pixels, NUM_LEDS);
}

static void set_all_color(struct led_rgb color)
{
    for (int i = 0; i < NUM_LEDS; i++)
    {
        pixels[i] = color;
    }
    led_strip_update_rgb(led_strip, pixels, NUM_LEDS);
}

static void running_light_effect(struct led_rgb color, uint32_t delay_ms, uint32_t duration_ms)
{
    uint32_t end_time = k_uptime_get_32() + duration_ms;
    while (k_uptime_get_32() < end_time && pattern_running)
    {
        clear_strip();
        for (int i = 0; i < NUM_LEDS && pattern_running; i++)
        {
            pixels[i] = color;
            led_strip_update_rgb(led_strip, pixels, NUM_LEDS);
            k_msleep(delay_ms);
            pixels[i] = (struct led_rgb){.r = 0, .g = 0, .b = 0};
        }
    }
    clear_strip();
}

static void breathing_effect(struct led_rgb color, uint32_t duration_ms)
{
    uint32_t end_time = k_uptime_get_32() + duration_ms;
    uint8_t max_brightness = 255;
    uint8_t step = 5;

    while (k_uptime_get_32() < end_time && pattern_running)
    {
        // Fade up
        for (uint8_t brightness = 0; brightness <= max_brightness && pattern_running; brightness += step)
        {
            struct led_rgb dimmed_color = {
                .r = (color.r * brightness) / max_brightness,
                .g = (color.g * brightness) / max_brightness,
                .b = (color.b * brightness) / max_brightness};
            set_all_color(dimmed_color);
            k_msleep(20);
        }

        // Fade down
        for (uint8_t brightness = max_brightness; brightness > 0 && pattern_running; brightness -= step)
        {
            struct led_rgb dimmed_color = {
                .r = (color.r * brightness) / max_brightness,
                .g = (color.g * brightness) / max_brightness,
                .b = (color.b * brightness) / max_brightness};
            set_all_color(dimmed_color);
            k_msleep(20);
        }
    }
    clear_strip();
}

static void rainbow_effect(uint32_t delay_ms, uint32_t duration_ms)
{
    uint32_t end_time = k_uptime_get_32() + duration_ms;
    uint8_t r = 255, g = 0, b = 0;

    while (k_uptime_get_32() < end_time && pattern_running)
    {
        // Create rainbow gradient across strip
        for (int i = 0; i < NUM_LEDS; i++)
        {
            pixels[i].r = r;
            pixels[i].g = g;
            pixels[i].b = b;

            // Advance color for next LED
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
        led_strip_update_rgb(led_strip, pixels, NUM_LEDS);
        k_msleep(delay_ms);
    }
    clear_strip();
}

static void sparkle_effect(struct led_rgb color, uint32_t delay_ms, uint32_t duration_ms)
{
    uint32_t end_time = k_uptime_get_32() + duration_ms;

    while (k_uptime_get_32() < end_time && pattern_running)
    {
        clear_strip();

        // Light up random LEDs
        for (int i = 0; i < NUM_LEDS / 3; i++)
        {
            int led = sys_rand32_get() % NUM_LEDS;
            pixels[led] = color;
        }

        led_strip_update_rgb(led_strip, pixels, NUM_LEDS);
        k_msleep(delay_ms);
    }
    clear_strip();
}

#if HAS_BUTTON
static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    pattern_running = false;
    k_msleep(100); // Debounce delay
    current_pattern = (current_pattern + 1) % 5;
    pattern_running = true;
    LOG_INF("Button pressed - switching to pattern %d", current_pattern);
}
#endif

static void pattern_thread(void)
{
    struct led_rgb red = {.r = 255, .g = 0, .b = 0};
    struct led_rgb green = {.r = 0, .g = 255, .b = 0};
    struct led_rgb blue = {.r = 0, .g = 0, .b = 255};
    struct led_rgb white = {.r = 255, .g = 255, .b = 255};

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
            set_all_color(red);
            k_msleep(2000);
            break;
        case 1:
            LOG_INF("Pattern 1: Running Green Light");
            running_light_effect(green, 100, 5000);
            break;
        case 2:
            LOG_INF("Pattern 2: Breathing Blue");
            breathing_effect(blue, 5000);
            break;
        case 3:
            LOG_INF("Pattern 3: Rainbow");
            rainbow_effect(50, 5000);
            break;
        case 4:
            LOG_INF("Pattern 4: White Sparkle");
            sparkle_effect(white, 100, 5000);
            break;
        default:
            current_pattern = 0;
            break;
        }

#if !HAS_BUTTON
        // Auto-advance patterns if no button
        k_msleep(1000);
        current_pattern = (current_pattern + 1) % 5;
#endif
    }
}

K_THREAD_DEFINE(pattern_tid, 2048, pattern_thread, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    LOG_INF("WS2812B LED Strip Demo Starting...");

    size_t device_count = z_device_get_all_static(&led_strip);
    if (!device_is_ready(led_strip))
    {
        LOG_ERR("LED strip device not ready");
        return -ENODEV;
    }
    LOG_INF("LED strip device ready - %d LEDs configured", NUM_LEDS);

    // Test the strip with a simple color
    set_all_color((struct led_rgb){.r = 50, .g = 0, .b = 0});
    k_msleep(500);
    clear_strip();
    LOG_INF("LED strip test completed");

    LOG_INF("Application initialization completed - starting pattern thread");
    return 0;
}