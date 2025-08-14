#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "keyasoftbox.h"

LOG_MODULE_REGISTER(gatt_service, LOG_LEVEL_INF);

/* External references */
extern struct keyasoftbox_state device_state;

/* Service and Characteristic UUIDs */
static struct bt_uuid_128 keyasoftbox_service_uuid = BT_UUID_INIT_128(KEYASOFTBOX_SERVICE_UUID);
static struct bt_uuid_128 led_control_char_uuid = BT_UUID_INIT_128(LED_CONTROL_CHAR_UUID);
static struct bt_uuid_128 led_status_char_uuid = BT_UUID_INIT_128(LED_STATUS_CHAR_UUID);
static struct bt_uuid_128 mesh_control_char_uuid = BT_UUID_INIT_128(MESH_CONTROL_CHAR_UUID);
static struct bt_uuid_128 device_info_char_uuid = BT_UUID_INIT_128(DEVICE_INFO_CHAR_UUID);

/* Notification enabled flags */
static bool led_status_notify_enabled = false;

/* JSON parsing helper functions */
static int find_json_value(const char *json, const char *key, char *value, size_t value_size)
{
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);
    
    const char *key_pos = strstr(json, search_key);
    if (!key_pos) {
        return -1;
    }
    
    const char *value_start = key_pos + strlen(search_key);
    
    /* Skip whitespace */
    while (*value_start == ' ' || *value_start == '\t') {
        value_start++;
    }
    
    /* Handle different value types */
    if (*value_start == '"') {
        /* String value */
        value_start++;
        const char *value_end = strchr(value_start, '"');
        if (!value_end) return -1;
        
        size_t len = MIN(value_end - value_start, value_size - 1);
        strncpy(value, value_start, len);
        value[len] = '\0';
    } else {
        /* Numeric or boolean value */
        const char *value_end = value_start;
        while (*value_end && *value_end != ',' && *value_end != '}' && *value_end != ' ') {
            value_end++;
        }
        
        size_t len = MIN(value_end - value_start, value_size - 1);
        strncpy(value, value_start, len);
        value[len] = '\0';
    }
    
    return 0;
}

static bool json_value_is_true(const char *value)
{
    return (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
}

/* GATT Characteristic Read/Write Handlers */
static ssize_t read_device_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    char info[256];
    snprintf(info, sizeof(info),
             "{"
             "\"device\":\"KeyaSoftBox\","
             "\"version\":\"1.0.0\","
             "\"mesh_addr\":\"0x%04x\","
             "\"provisioned\":%s,"
             "\"pixels\":%d,"
             "\"effects\":%d"
             "}",
             device_state.mesh_addr,
             device_state.mesh_provisioned ? "true" : "false",
             NUM_PIXELS,
             EFFECT_MAX);
    
    return bt_gatt_attr_read(conn, attr, buf, len, offset, info, strlen(info));
}

static ssize_t read_led_status(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              void *buf, uint16_t len, uint16_t offset)
{
    char status[512];
    snprintf(status, sizeof(status),
             "{"
             "\"power\":%s,"
             "\"brightness\":%d,"
             "\"color\":{"
             "\"r\":%d,\"g\":%d,\"b\":%d"
             "},"
             "\"auto_mode\":%s,"
             "\"effect\":%d,"
             "\"speed\":%u,"
             "\"current_pixels\":["
             "{\"r\":%d,\"g\":%d,\"b\":%d},"
             "{\"r\":%d,\"g\":%d,\"b\":%d},"
             "{\"r\":%d,\"g\":%d,\"b\":%d},"
             "{\"r\":%d,\"g\":%d,\"b\":%d}"
             "]"
             "}",
             device_state.power_on ? "true" : "false",
             device_state.brightness,
             device_state.static_color.r, device_state.static_color.g, device_state.static_color.b,
             device_state.auto_mode ? "true" : "false",
             device_state.animation_type,
             device_state.animation_speed,
             /* First 4 pixels current state */
             device_state.pixels[0].r, device_state.pixels[0].g, device_state.pixels[0].b,
             device_state.pixels[1].r, device_state.pixels[1].g, device_state.pixels[1].b,
             device_state.pixels[2].r, device_state.pixels[2].g, device_state.pixels[2].b,
             device_state.pixels[3].r, device_state.pixels[3].g, device_state.pixels[3].b);
    
    return bt_gatt_attr_read(conn, attr, buf, len, offset, status, strlen(status));
}

static ssize_t write_led_control(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset,
                                uint8_t flags)
{
    if (len == 0 || len > 512) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    
    char json_cmd[513];
    memcpy(json_cmd, buf, len);
    json_cmd[len] = '\0';
    
    LOG_INF("LED Control command: %s", json_cmd);
    
    parse_led_control_json(json_cmd, len);
    
    return len;
}

static ssize_t write_mesh_control(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len, uint16_t offset,
                                 uint8_t flags)
{
    if (len == 0 || len > 512) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    
    char json_cmd[513];
    memcpy(json_cmd, buf, len);
    json_cmd[len] = '\0';
    
    LOG_INF("Mesh Control command: %s", json_cmd);
    
    parse_mesh_control_json(json_cmd, len);
    
    return len;
}

/* CCC (Client Characteristic Configuration) callback for notifications */
static void led_status_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    led_status_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("LED Status notifications %s", led_status_notify_enabled ? "enabled" : "disabled");
}

/* GATT Service Definition */
BT_GATT_SERVICE_DEFINE(keyasoftbox_svc,
    BT_GATT_PRIMARY_SERVICE(&keyasoftbox_service_uuid),
    
    /* LED Control Characteristic */
    BT_GATT_CHARACTERISTIC(&led_control_char_uuid.uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                          BT_GATT_PERM_WRITE, NULL, write_led_control, NULL),
    
    /* LED Status Characteristic */
    BT_GATT_CHARACTERISTIC(&led_status_char_uuid.uuid,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ, read_led_status, NULL, NULL),
    BT_GATT_CCC(led_status_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    
    /* Mesh Control Characteristic */
    BT_GATT_CHARACTERISTIC(&mesh_control_char_uuid.uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                          BT_GATT_PERM_WRITE, NULL, write_mesh_control, NULL),
    
    /* Device Info Characteristic */
    BT_GATT_CHARACTERISTIC(&device_info_char_uuid.uuid,
                          BT_GATT_CHRC_READ,
                          BT_GATT_PERM_READ, read_device_info, NULL, NULL),
);

/* JSON Command Parsing Functions */
void parse_led_control_json(const char *json_data, uint16_t len)
{
    char value[32];
    bool state_changed = false;
    
    /* Parse power */
    if (find_json_value(json_data, "power", value, sizeof(value)) == 0) {
        bool new_power = json_value_is_true(value);
        if (new_power != device_state.power_on) {
            device_state.power_on = new_power;
            state_changed = true;
        }
    }
    
    /* Parse brightness */
    if (find_json_value(json_data, "brightness", value, sizeof(value)) == 0) {
        uint8_t new_brightness = atoi(value);
        if (new_brightness != device_state.brightness) {
            device_state.brightness = new_brightness;
            state_changed = true;
        }
    }
    
    /* Parse color */
    bool color_changed = false;
    if (find_json_value(json_data, "r", value, sizeof(value)) == 0) {
        device_state.static_color.r = atoi(value);
        color_changed = true;
    }
    if (find_json_value(json_data, "g", value, sizeof(value)) == 0) {
        device_state.static_color.g = atoi(value);
        color_changed = true;
    }
    if (find_json_value(json_data, "b", value, sizeof(value)) == 0) {
        device_state.static_color.b = atoi(value);
        color_changed = true;
    }
    
    /* Parse auto mode */
    if (find_json_value(json_data, "auto_mode", value, sizeof(value)) == 0) {
        bool new_auto = json_value_is_true(value);
        if (new_auto != device_state.auto_mode) {
            device_state.auto_mode = new_auto;
            state_changed = true;
        }
    }
    
    /* Parse effect type */
    if (find_json_value(json_data, "effect", value, sizeof(value)) == 0) {
        uint8_t new_effect = atoi(value);
        if (new_effect < EFFECT_MAX && new_effect != device_state.animation_type) {
            device_state.animation_type = new_effect;
            state_changed = true;
        }
    }
    
    /* Parse speed */
    if (find_json_value(json_data, "speed", value, sizeof(value)) == 0) {
        uint32_t new_speed = atoi(value);
        if (new_speed != device_state.animation_speed) {
            device_state.animation_speed = new_speed;
            state_changed = true;
        }
    }
    
    /* Apply changes */
    if (color_changed || state_changed) {
        if (device_state.auto_mode) {
            /* Start animation */
            reset_animation_state();
            k_work_reschedule(&animation_work, K_MSEC(100));
        } else {
            /* Set static color */
            k_work_cancel_delayable(&animation_work);
            if (device_state.power_on) {
                set_all_pixels(device_state.static_color.r, 
                              device_state.static_color.g, 
                              device_state.static_color.b);
            } else {
                clear_all_pixels();
            }
            update_led_strip();
        }
        
        /* Notify status change */
        notify_led_status_change();
    }
}

void parse_mesh_control_json(const char *json_data, uint16_t len)
{
    char value[32];
    
    /* Parse the command and forward to mesh */
    bool send_mesh_cmd = false;
    uint8_t power = device_state.power_on ? 1 : 0;
    uint8_t brightness = device_state.brightness;
    uint8_t r = device_state.static_color.r;
    uint8_t g = device_state.static_color.g;
    uint8_t b = device_state.static_color.b;
    
    if (find_json_value(json_data, "power", value, sizeof(value)) == 0) {
        power = json_value_is_true(value) ? 1 : 0;
        send_mesh_cmd = true;
    }
    
    if (find_json_value(json_data, "brightness", value, sizeof(value)) == 0) {
        brightness = atoi(value);
        send_mesh_cmd = true;
    }
    
    if (find_json_value(json_data, "r", value, sizeof(value)) == 0) {
        r = atoi(value);
        send_mesh_cmd = true;
    }
    
    if (find_json_value(json_data, "g", value, sizeof(value)) == 0) {
        g = atoi(value);
        send_mesh_cmd = true;
    }
    
    if (find_json_value(json_data, "b", value, sizeof(value)) == 0) {
        b = atoi(value);
        send_mesh_cmd = true;
    }
    
    /* Check for effect command */
    if (find_json_value(json_data, "effect", value, sizeof(value)) == 0) {
        uint8_t effect_type = atoi(value);
        uint32_t speed = 1000; /* Default speed */
        
        if (find_json_value(json_data, "speed", value, sizeof(value)) == 0) {
            speed = atoi(value);
        }
        
        mesh_send_effect_command(effect_type, speed);
        return;
    }
    
    if (send_mesh_cmd) {
        mesh_send_led_command(power, brightness, r, g, b);
    }
}

/* Notification function */
void notify_led_status_change(void)
{
    if (!led_status_notify_enabled) {
        return;
    }
    
    /* Get the LED status characteristic attribute */
    const struct bt_gatt_attr *attr = &keyasoftbox_svc.attrs[3]; /* LED Status characteristic */
    
    /* Prepare status data */
    char status[256];
    snprintf(status, sizeof(status),
             "{"
             "\"power\":%s,"
             "\"brightness\":%d,"
             "\"r\":%d,\"g\":%d,\"b\":%d,"
             "\"auto_mode\":%s,"
             "\"effect\":%d"
             "}",
             device_state.power_on ? "true" : "false",
             device_state.brightness,
             device_state.static_color.r, device_state.static_color.g, device_state.static_color.b,
             device_state.auto_mode ? "true" : "false",
             device_state.animation_type);
    
    /* Send notification */
    int err = bt_gatt_notify(NULL, attr, status, strlen(status));
    if (err) {
        LOG_WRN("Failed to send notification: %d", err);
    }
}

/* GATT Service Initialization */
int gatt_service_init(void)
{
    LOG_INF("KeyaSoftBox GATT service initialized");
    return 0;
}