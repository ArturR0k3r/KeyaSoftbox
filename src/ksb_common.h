#ifndef KSB_COMMON_H
#define KSB_COMMON_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/led_strip.h>
#include "version.h"

// System configuration
#define KSB_MAX_NETWORK_NAME_LEN 32
#define KSB_MAX_MESH_NODES 8
#define KSB_LED_COUNT 8
#define KSB_LED_UPDATE_RATE_MS 33

// Network configuration
#define KSB_AP_SSID_PREFIX "KSB_Setup_"
#define KSB_AP_PASSWORD "keya1234"
#define KSB_WEB_PORT 80
#define KSB_MESH_PORT 8080

// Hardware pins
#define KSB_USER_BUTTON_PIN 0
#define KSB_STATUS_LED_RED_PIN 2
#define KSB_STATUS_LED_GREEN_PIN 3

// System states
enum ksb_system_state
{
    KSB_STATE_SYSTEM_INIT,
    KSB_STATE_CONFIG_MODE,
    KSB_STATE_NETWORK_SCAN,
    KSB_STATE_MESH_CLIENT,
    KSB_STATE_MESH_MASTER,
    KSB_STATE_OPERATIONAL,
    KSB_STATE_CONNECTION_LOST,
    KSB_STATE_ERROR_RECOVERY
};

// LED patterns
enum ksb_led_pattern
{
    KSB_PATTERN_OFF,
    KSB_PATTERN_SOLID,
    KSB_PATTERN_BREATHING,
    KSB_PATTERN_RUNNING_LIGHT,
    KSB_PATTERN_RAINBOW,
    KSB_PATTERN_SPARKLE,
    KSB_PATTERN_WAVE,
    KSB_PATTERN_COUNT
};

// LED command structure for mesh communication
struct ksb_led_command
{
    enum ksb_led_pattern pattern;
    struct led_rgb color;
    uint32_t speed;
    uint32_t brightness;
    uint32_t frame;
} __packed;

// Network configuration
struct ksb_network_config
{
    char network_name[KSB_MAX_NETWORK_NAME_LEN];
    bool is_configured;
    uint8_t device_id;
} __packed;

// Global system context
struct ksb_context
{
    enum ksb_system_state current_state;
    struct ksb_network_config config;
    struct k_sem state_lock;
    bool system_running;
};

extern struct ksb_context g_ksb_ctx;

#endif // KSB_COMMON_H