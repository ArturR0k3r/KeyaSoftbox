#ifndef KEYASOFTBOX_H
#define KEYASOFTBOX_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/bluetooth/mesh.h>
#include <stdint.h>
#include <stdbool.h>

/* Device Configuration */
#define NUM_PIXELS 8
#define DEVICE_NAME "KeyaSoftBox"

/* Custom Mesh Model ID */
#define BT_MESH_MODEL_ID_KEYA_SOFTBOX 0x8001

/* LED Effect Types */
enum led_effect_type {
    EFFECT_STATIC = 0,
    EFFECT_RAINBOW_CYCLE = 1,
    EFFECT_BREATHING = 2,
    EFFECT_COLOR_WIPE = 3,
    EFFECT_RAINBOW_WAVE = 4,
    EFFECT_FIRE = 5,
    EFFECT_TWINKLE = 6,
    EFFECT_CHASE = 7,
    EFFECT_PULSE_COLORS = 8,
    EFFECT_MAX
};

/* Device State Structure */
struct keyasoftbox_state {
    bool mesh_provisioned;
    uint16_t mesh_addr;
    uint16_t net_idx;
    uint16_t app_idx;
    struct led_rgb pixels[NUM_PIXELS];
    uint8_t brightness;
    bool auto_mode;
    uint8_t animation_type;
    uint32_t animation_speed;
    bool power_on;
    struct led_rgb static_color;
};

/* Custom Mesh Opcodes */
#define KEYASOFTBOX_LED_SET_OP BT_MESH_MODEL_OP_3(0x80, 0x01, 0x00)
#define KEYASOFTBOX_LED_STATUS_OP BT_MESH_MODEL_OP_3(0x80, 0x01, 0x01)
#define KEYASOFTBOX_LED_GET_OP BT_MESH_MODEL_OP_3(0x80, 0x01, 0x02)

/* BLE Service UUIDs */
#define KEYASOFTBOX_SERVICE_UUID BT_UUID_128_ENCODE(0x6ba75d00, 0x3f4a, 0x4c8e, 0x8e1b, 0x123456789abc)
#define LED_CONTROL_CHAR_UUID BT_UUID_128_ENCODE(0x6ba75d01, 0x3f4a, 0x4c8e, 0x8e1b, 0x123456789abc)
#define LED_STATUS_CHAR_UUID BT_UUID_128_ENCODE(0x6ba75d02, 0x3f4a, 0x4c8e, 0x8e1b, 0x123456789abc)
#define MESH_CONTROL_CHAR_UUID BT_UUID_128_ENCODE(0x6ba75d03, 0x3f4a, 0x4c8e, 0x8e1b, 0x123456789abc)
#define DEVICE_INFO_CHAR_UUID BT_UUID_128_ENCODE(0x6ba75d04, 0x3f4a, 0x4c8e, 0x8e1b, 0x123456789abc)

/* Function Prototypes */

/* LED Effects Functions (led_effects.c) */
void run_led_effect(uint8_t effect_type);
void reset_animation_state(void);
void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, struct led_rgb *rgb);
uint8_t scale8(uint8_t value, uint8_t scale);

/* Individual effect functions */
void effect_static_color(void);
void effect_rainbow_cycle(void);
void effect_breathing(void);
void effect_color_wipe(void);
void effect_rainbow_wave(void);
void effect_fire(void);
void effect_twinkle(void);
void effect_chase(void);
void effect_pulse_colors(void);

/* Mesh Handler Functions (mesh_handler.c) */
int mesh_init_keyasoftbox(void);
void mesh_send_led_command(uint8_t power, uint8_t brightness, uint8_t r, uint8_t g, uint8_t b);
void mesh_send_effect_command(uint8_t effect_type, uint32_t speed);

/* GATT Service Functions (gatt_service.c) */
int gatt_service_init(void);
void notify_led_status_change(void);
void parse_led_control_json(const char *json_data, uint16_t len);
void parse_mesh_control_json(const char *json_data, uint16_t len);

/* Utility Functions */
void update_led_strip(void);
void set_pixel_color(int pixel, uint8_t r, uint8_t g, uint8_t b);
void set_all_pixels(uint8_t r, uint8_t g, uint8_t b);
void clear_all_pixels(void);

/* Global Variables (defined in main.c) */
extern struct keyasoftbox_state device_state;
extern const struct device *const strip;
extern struct k_work_delayable animation_work;

/* Macros */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#endif /* KEYASOFTBOX_H */