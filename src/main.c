#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include "ksb_common.h"
#include "state_machine.h"
#include "nvs_storage.h"
#include "../ws2812/ws2812_driver.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

// Global system context
struct ksb_context g_ksb_ctx = {
    .current_state = KSB_STATE_SYSTEM_INIT,
    .system_running = true};

// Hardware devices
static const struct device *button_dev;
static const struct device *status_red_dev;
static const struct device *status_green_dev;
static const struct device *gpio_dev;
static struct gpio_callback button_cb_data;

// Button handler
static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    LOG_INF("User button pressed");
    // Cycle through LED patterns in operational mode
    if (g_ksb_ctx.current_state == KSB_STATE_OPERATIONAL)
    {
        led_control_next_pattern();
    }
}

// Status LED control
static void set_status_leds(bool red, bool green)
{
    if (gpio_dev)
    {
        gpio_pin_set(gpio_dev, 23, red ? 1 : 0);   // user_led1
        gpio_pin_set(gpio_dev, 22, green ? 1 : 0); // user_led2
    }
}

// Status LED thread
static void status_led_thread(void)
{
    while (g_ksb_ctx.system_running)
    {
        switch (g_ksb_ctx.current_state)
        {
        case KSB_STATE_SYSTEM_INIT:
            // Fast red blink during init
            set_status_leds(true, false);
            k_msleep(100);
            set_status_leds(false, false);
            k_msleep(100);
            break;

        case KSB_STATE_CONFIG_MODE:
            // Alternating red/green in config mode
            set_status_leds(true, false);
            k_msleep(500);
            set_status_leds(false, true);
            k_msleep(500);
            break;

        case KSB_STATE_NETWORK_SCAN:
        case KSB_STATE_MESH_CLIENT:
        case KSB_STATE_MESH_MASTER:
            // Slow green blink during connection
            set_status_leds(false, true);
            k_msleep(250);
            set_status_leds(false, false);
            k_msleep(1750);
            break;

        case KSB_STATE_OPERATIONAL:
            // Solid green when operational
            set_status_leds(false, true);
            k_msleep(1000);
            break;

        case KSB_STATE_CONNECTION_LOST:
        case KSB_STATE_ERROR_RECOVERY:
            // Fast red blink on error
            set_status_leds(true, false);
            k_msleep(200);
            set_status_leds(false, false);
            k_msleep(200);
            break;

        default:
            set_status_leds(false, false);
            k_msleep(1000);
            break;
        }
    }
}

K_THREAD_DEFINE(status_led_tid, 1024, status_led_thread, NULL, NULL, NULL, 7, 0, 0);

// Hardware initialization
static int init_hardware(void)
{
    int ret;

    // Get GPIO device for button and LEDs
    gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(gpio_dev))
    {
        LOG_ERR("GPIO device not ready");
        return -ENODEV;
    }

    // Configure button pin
    ret = gpio_pin_configure(gpio_dev, 21, GPIO_INPUT | GPIO_PULL_UP);
    if (ret != 0)
    {
        LOG_ERR("Failed to configure button pin");
        return ret;
    }

    ret = gpio_pin_interrupt_configure(gpio_dev, 21, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0)
    {
        LOG_ERR("Failed to configure button interrupt");
        return ret;
    }

    gpio_init_callback(&button_cb_data, button_pressed, BIT(21));
    gpio_add_callback(gpio_dev, &button_cb_data);

    // Configure status LED pins
    ret = gpio_pin_configure(gpio_dev, 23, GPIO_OUTPUT_INACTIVE); // user_led1
    if (ret != 0)
    {
        LOG_ERR("Failed to configure red LED pin");
        return ret;
    }
    ret = gpio_pin_configure(gpio_dev, 22, GPIO_OUTPUT_INACTIVE); // user_led2
    if (ret != 0)
    {
        LOG_ERR("Failed to configure green LED pin");
        return ret;
    }

    LOG_INF("Hardware initialized successfully");
    return 0;
}

int main(void)
{
    int ret;

    LOG_INF("KSB v%s starting...", KSB_VERSION_STRING);
    LOG_INF("Build: %s %s", KSB_BUILD_DATE, KSB_BUILD_TIME);

    // Initialize semaphores
    k_sem_init(&g_ksb_ctx.state_lock, 1, 1);

    // Initialize NVS storage
    ret = nvs_storage_init();
    if (ret != 0)
    {
        LOG_ERR("Failed to initialize NVS storage: %d", ret);
        return ret;
    }

    // Load configuration
    ret = nvs_storage_load_config(&g_ksb_ctx.config);
    if (ret != 0)
    {
        LOG_WRN("No valid configuration found, using defaults");
        memset(&g_ksb_ctx.config, 0, sizeof(g_ksb_ctx.config));
        g_ksb_ctx.config.is_configured = false;
        g_ksb_ctx.config.device_id = sys_rand32_get() & 0xFF;
    }

    // Initialize hardware
    ret = init_hardware();
    if (ret != 0)
    {
        LOG_ERR("Hardware initialization failed: %d", ret);
        return ret;
    }

    // Initialize LED control
    ret = led_control_init();
    if (ret != 0)
    {
        LOG_ERR("LED control initialization failed: %d", ret);
        return ret;
    }

    // Show startup pattern
    struct led_rgb startup_color = {50, 0, 50}; // Purple
    led_control_set_pattern(KSB_PATTERN_SOLID, startup_color, 255, 0);
    k_msleep(1000);
    led_control_set_pattern(KSB_PATTERN_OFF, (struct led_rgb){0, 0, 0}, 0, 0);

    // Initialize state machine
    ret = state_machine_init();
    if (ret != 0)
    {
        LOG_ERR("State machine initialization failed: %d", ret);
        return ret;
    }

    LOG_INF("KSB initialization complete");

    // Start state machine
    state_machine_start();

    return 0;
}