#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/sys/printk.h>
#include <zephyr/settings/settings.h>
#include <string.h>
#include <stdio.h>
#include "keyasoftbox.h"

LOG_MODULE_REGISTER(keyasoftbox, LOG_LEVEL_INF);

/* LED Strip Device */
static const struct device *const strip = DEVICE_DT_GET(DT_ALIAS(led_strip));

/* GPIO Configuration */
#define BUTTON_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(BUTTON_NODE, gpios, {0});
static struct gpio_callback button_cb_data;

/* Device State */
struct keyasoftbox_state device_state = {
    .mesh_provisioned = false,
    .mesh_addr = 0,
    .net_idx = 0,
    .app_idx = 0,
    .brightness = 255,
    .auto_mode = false,
    .animation_type = EFFECT_STATIC,
    .animation_speed = 1000,
    .power_on = true,
    .static_color = {255, 255, 255}, // White default
};

/* Work Queue for Animations */
K_WORK_DELAYABLE_DEFINE(animation_work, animation_work_handler);

/* Forward Declarations */
static void animation_work_handler(struct k_work *work);

/* Mesh Model Operations */
extern const struct bt_mesh_model_op keyasoftbox_model_op[];
extern struct bt_mesh_model_pub keyasoftbox_led_pub;

/* Configuration Server */
static struct bt_mesh_cfg_srv cfg_srv = {
    .relay = BT_MESH_RELAY_ENABLED,
    .beacon = BT_MESH_BEACON_ENABLED,
    .frnd = BT_MESH_FRIEND_NOT_SUPPORTED,
    .gatt_proxy = BT_MESH_GATT_PROXY_ENABLED,
    .default_ttl = 7,
    .net_transmit = BT_MESH_TRANSMIT(2, 20),
    .relay_retransmit = BT_MESH_TRANSMIT(2, 20),
};

/* Health Server */
static struct bt_mesh_health_srv health_srv = {};
BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

/* Models */
static struct bt_mesh_model root_models[] = {
    BT_MESH_MODEL_CFG_SRV(&cfg_srv),
    BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
    BT_MESH_MODEL(BT_MESH_MODEL_ID_KEYA_SOFTBOX, keyasoftbox_model_op,
                  &keyasoftbox_led_pub, NULL),
};

/* Export model reference */
struct bt_mesh_model *keyasoftbox_model = &root_models[2];

/* Elements */
static struct bt_mesh_elem elements[] = {
    BT_MESH_ELEM(0, root_models, BT_MESH_MODEL_NONE),
};

/* Node composition */
const struct bt_mesh_comp comp = {
    .cid = BT_COMP_ID_LF,
    .pid = 0x1234,
    .vid = 0x0001,
    .elem = elements,
    .elem_count = ARRAY_SIZE(elements),
};

/* LED Control Functions */
void set_pixel_color(int pixel, uint8_t r, uint8_t g, uint8_t b)
{
    if (pixel >= 0 && pixel < NUM_PIXELS) {
        device_state.pixels[pixel].r = (r * device_state.brightness) / 255;
        device_state.pixels[pixel].g = (g * device_state.brightness) / 255;
        device_state.pixels[pixel].b = (b * device_state.brightness) / 255;
    }
}

void set_all_pixels(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < NUM_PIXELS; i++) {
        set_pixel_color(i, r, g, b);
    }
}

void clear_all_pixels(void)
{
    memset(device_state.pixels, 0, sizeof(device_state.pixels));
}

void update_led_strip(void)
{
    if (!device_is_ready(strip)) {
        LOG_ERR("LED strip device not ready");
        return;
    }
    
    if (device_state.power_on) {
        led_strip_update_rgb(strip, device_state.pixels, NUM_PIXELS);
    } else {
        struct led_rgb off_pixels[NUM_PIXELS];
        memset(off_pixels, 0, sizeof(off_pixels));
        led_strip_update_rgb(strip, off_pixels, NUM_PIXELS);
    }
}

/* Animation Work Handler */
static void animation_work_handler(struct k_work *work)
{
    if (!device_state.auto_mode || !device_state.power_on) {
        return;
    }
    
    run_led_effect(device_state.animation_type);
    update_led_strip();
    
    k_work_reschedule(&animation_work, K_MSEC(device_state.animation_speed));
}

/* Button Handler */
static void button_pressed(const struct device *dev, struct gpio_callback *cb,
                          uint32_t pins)
{
    static int color_index = 0;
    const struct led_rgb colors[] = {
        {255, 0, 0},   // Red
        {0, 255, 0},   // Green
        {0, 0, 255},   // Blue
        {255, 255, 0}, // Yellow
        {255, 0, 255}, // Magenta
        {0, 255, 255}, // Cyan
        {255, 255, 255}, // White
        {0, 0, 0}      // Off
    };
    
    LOG_INF("Button pressed - changing color");
    
    device_state.auto_mode = false;
    k_work_cancel_delayable(&animation_work);
    
    struct led_rgb color = colors[color_index];
    device_state.static_color = color;
    device_state.power_on = (color_index != 7); // Off for black
    
    if (device_state.power_on) {
        set_all_pixels(color.r, color.g, color.b);
    } else {
        clear_all_pixels();
    }
    update_led_strip();
    
    color_index = (color_index + 1) % ARRAY_SIZE(colors);
    
    /* Notify connected devices */
    notify_led_status_change();
}

/* Provisioning Callbacks */
static void prov_complete(uint16_t net_idx, uint16_t addr)
{
    LOG_INF("Provisioning complete! Net IDX: 0x%04x, Addr: 0x%04x", net_idx, addr);
    
    device_state.mesh_provisioned = true;
    device_state.net_idx = net_idx;
    device_state.mesh_addr = addr;
    
    /* Celebration animation */
    for (int i = 0; i < 3; i++) {
        set_all_pixels(0, 255, 0);  // Green
        update_led_strip();
        k_sleep(K_MSEC(200));
        clear_all_pixels();
        update_led_strip();
        k_sleep(K_MSEC(200));
    }
}

static void prov_reset(void)
{
    LOG_INF("Provisioning reset");
    device_state.mesh_provisioned = false;
    device_state.mesh_addr = 0;
    device_state.net_idx = 0;
}

static int output_number(bt_mesh_output_action_t action, uint32_t number)
{
    LOG_INF("OOB Number: %u", number);
    
    /* Display number by blinking LEDs */
    clear_all_pixels();
    update_led_strip();
    k_sleep(K_MSEC(500));
    
    uint8_t blinks = number % 10;
    if (blinks == 0) blinks = 10;
    
    for (int i = 0; i < blinks; i++) {
        set_all_pixels(0, 0, 255);  // Blue
        update_led_strip();
        k_sleep(K_MSEC(300));
        clear_all_pixels();
        update_led_strip();
        k_sleep(K_MSEC(300));
    }
    
    return 0;
}

/* Provisioning Structure */
static const struct bt_mesh_prov prov = {
    .uuid = NULL, // Will be set in main
    .output_size = 4,
    .output_actions = BT_MESH_DISPLAY_NUMBER | BT_MESH_BLINK,
    .output_number = output_number,
    .complete = prov_complete,
    .reset = prov_reset,
};

/* BLE Advertisement */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_MESH_PROV_VAL)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, sizeof(DEVICE_NAME) - 1),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, KEYASOFTBOX_SERVICE_UUID),
};

/* Connection Callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err 0x%02x)", err);
        return;
    }
    LOG_INF("Connected to phone/controller");
    
    /* Brief connection indication */
    if (!device_state.auto_mode) {
        set_all_pixels(255, 255, 0);  // Yellow
        update_led_strip();
        k_sleep(K_MSEC(500));
        set_all_pixels(device_state.static_color.r, 
                      device_state.static_color.g, 
                      device_state.static_color.b);
        update_led_strip();
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason 0x%02x)", reason);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* Initialization Functions */
static int gpio_init(void)
{
    int ret;
    
    if (!device_is_ready(button.port)) {
        LOG_ERR("Button GPIO device not ready");
        return -ENODEV;
    }
    
    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Error configuring button GPIO: %d", ret);
        return ret;
    }
    
    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Error configuring button interrupt: %d", ret);
        return ret;
    }
    
    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
    
    return 0;
}

static int led_strip_init(void)
{
    if (!device_is_ready(strip)) {
        LOG_ERR("LED strip device not ready");
        return -ENODEV;
    }
    
    LOG_INF("LED strip initialized with %d pixels", NUM_PIXELS);
    
    /* Initial LED test sequence */
    const struct led_rgb test_colors[] = {
        {50, 0, 0},   // Red
        {0, 50, 0},   // Green  
        {0, 0, 50},   // Blue
        {0, 0, 0}     // Off
    };
    
    for (int c = 0; c < ARRAY_SIZE(test_colors); c++) {
        for (int i = 0; i < NUM_PIXELS; i++) {
            device_state.pixels[i] = test_colors[c];
        }
        led_strip_update_rgb(strip, device_state.pixels, NUM_PIXELS);
        k_sleep(K_MSEC(200));
    }
    
    return 0;
}

static int mesh_init(void)
{
    int err;
    
    /* Generate unique UUID */
    static uint8_t dev_uuid[16] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22,
                                   0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00 };
    
    /* Try to make UUID more unique */
    uint32_t device_id = sys_rand32_get();
    memcpy(&dev_uuid[12], &device_id, 4);
    
    struct bt_mesh_prov prov_handlers = prov;
    prov_handlers.uuid = dev_uuid;
    
    err = bt_mesh_init(&prov_handlers, &comp);
    if (err) {
        LOG_ERR("Mesh initialization failed (err %d)", err);
        return err;
    }
    
    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
        settings_load();
    }
    
    bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
    
    LOG_INF("Bluetooth mesh initialized");
    return 0;
}

/* Main Function */
int main(void)
{
    int err;
    
    LOG_INF("Starting KeyaSoftBox v1.0");
    LOG_INF("Build: " __DATE__ " " __TIME__);
    
    /* Initialize LED strip first */
    err = led_strip_init();
    if (err) {
        LOG_ERR("LED strip initialization failed");
        return err;
    }
    
    /* Initialize GPIO */
    err = gpio_init();
    if (err) {
        LOG_ERR("GPIO initialization failed");
        return err;
    }
    
    /* Initialize Bluetooth */
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }
    
    LOG_INF("Bluetooth initialized");
    
    /* Initialize GATT Service */
    err = gatt_service_init();
    if (err) {
        LOG_ERR("GATT service initialization failed");
        return err;
    }
    
    /* Initialize Mesh */
    err = mesh_init();
    if (err) {
        LOG_ERR("Mesh initialization failed");
        return err;
    }
    
    /* Start advertising */
    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad),
                         sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return err;
    }
    
    LOG_INF("KeyaSoftBox ready - advertising started");
    LOG_INF("Features: %d WS2812B LEDs, BLE Mesh, Phone Control", NUM_PIXELS);
    LOG_INF("Available effects: %d", EFFECT_MAX);
    
    /* Set initial state */
    set_all_pixels(device_state.static_color.r, 
                  device_state.static_color.g, 
                  device_state.static_color.b);
    update_led_strip();
    
    /* Main loop */
    while (1) {
        k_sleep(K_SECONDS(1));
        
        /* Heartbeat for unprovisioned devices */
        if (!device_state.mesh_provisioned) {
            static int heartbeat_counter = 0;
            if (++heartbeat_counter >= 10) {  // Every 10 seconds
                heartbeat_counter = 0;
                
                /* Brief blue flash to indicate waiting for provisioning */
                if (!device_state.auto_mode) {
                    struct led_rgb original_color = device_state.pixels[0];
                    set_pixel_color(0, 0, 0, 100);  // First LED blue
                    update_led_strip();
                    k_sleep(K_MSEC(100));
                    device_state.pixels[0] = original_color;
                    update_led_strip();
                }
            }
        }
    }
    
    return 0;
}_MESH_MODEL(BT_MESH_MODEL_ID_KEYA_SOFTBOX, keyasoftbox_led_op,
                  &keyasoftbox_led_pub, NULL),
};

/* Elements */
static struct bt_mesh_elem elements[] = {
    BT_MESH_ELEM(0, root_models, BT_MESH_MODEL_NONE),
};

/* Node composition */
static const struct bt_mesh_comp comp = {
    .cid = BT_COMP_ID_LF,
    .pid = 0x1234,
    .vid = 0x0001,
    .elem = elements,
    .elem_count = ARRAY_SIZE(elements),
};

/* Provisioning */
static uint8_t dev_uuid[16] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };

static const struct bt_mesh_prov prov = {
    .uuid = dev_uuid,
    .output_size = 4,
    .output_actions = BT_MESH_DISPLAY_NUMBER | BT_MESH_BLINK,
    .input_size = 4,
    .input_actions = BT_MESH_ENTER_NUMBER,
};

/* LED Control Functions */
static void set_pixel_color(int pixel, uint8_t r, uint8_t g, uint8_t b)
{
    if (pixel >= 0 && pixel < NUM_PIXELS) {
        device_state.pixels[pixel].r = (r * device_state.brightness) / 255;
        device_state.pixels[pixel].g = (g * device_state.brightness) / 255;
        device_state.pixels[pixel].b = (b * device_state.brightness) / 255;
    }
}

static void set_all_pixels(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < NUM_PIXELS; i++) {
        set_pixel_color(i, r, g, b);
    }
}

static void clear_all_pixels(void)
{
    memset(device_state.pixels, 0, sizeof(device_state.pixels));
}

static void update_led_strip(void)
{
    if (!device_get_binding(LED_STRIP)) {
        LOG_ERR("LED strip device not found");
        return;
    }
    
    if (device_state.power_on) {
        led_strip_update_rgb(strip, device_state.pixels, NUM_PIXELS);
    } else {
        struct led_rgb off_pixels[NUM_PIXELS];
        memset(off_pixels, 0, sizeof(off_pixels));
        led_strip_update_rgb(strip, off_pixels, NUM_PIXELS);
    }
}

/* Animation Functions */
static void rainbow_animation(void)
{
    static uint8_t hue_offset = 0;
    
    for (int i = 0; i < NUM_PIXELS; i++) {
        uint8_t hue = (hue_offset + (i * 255 / NUM_PIXELS)) % 255;
        
        // Simple HSV to RGB conversion
        uint8_t r, g, b;
        uint8_t region = hue / 43;
        uint8_t remainder = (hue % 43) * 6;
        
        switch (region) {
            case 0: r = 255; g = remainder; b = 0; break;
            case 1: r = 255 - remainder; g = 255; b = 0; break;
            case 2: r = 0; g = 255; b = remainder; break;
            case 3: r = 0; g = 255 - remainder; b = 255; break;
            case 4: r = remainder; g = 0; b = 255; break;
            default: r = 255; g = 0; b = 255 - remainder; break;
        }
        
        set_pixel_color(i, r, g, b);
    }
    
    hue_offset += 5;
    update_led_strip();
}

static void breathing_animation(void)
{
    static uint8_t breath_step = 0;
    static bool breath_up = true;
    
    uint8_t intensity = breath_up ? breath_step : (255 - breath_step);
    set_all_pixels(intensity, intensity, intensity);
    update_led_strip();
    
    if (breath_up) {
        breath_step += 5;
        if (breath_step >= 255) breath_up = false;
    } else {
        breath_step -= 5;
        if (breath_step <= 0) breath_up = true;
    }
}

static void animation_work_handler(struct k_work *work)
{
    if (!device_state.auto_mode || !device_state.power_on) {
        return;
    }
    
    switch (device_state.animation_type) {
        case 1:
            rainbow_animation();
            break;
        case 2:
            breathing_animation();
            break;
        default:
            break;
    }
    
    k_work_reschedule(&animation_work, K_MSEC(device_state.animation_speed));
}

/* Mesh Message Handlers */
static void keyasoftbox_led_get(struct bt_mesh_model *model,
                               struct bt_mesh_msg_ctx *ctx,
                               struct net_buf_simple *buf)
{
    struct net_buf_simple *msg = NET_BUF_SIMPLE(2 + 16);
    
    LOG_INF("LED Get request from 0x%04x", ctx->addr);
    
    bt_mesh_model_msg_init(msg, KEYASOFTBOX_LED_STATUS_OP);
    net_buf_simple_add_u8(msg, device_state.power_on);
    net_buf_simple_add_u8(msg, device_state.brightness);
    net_buf_simple_add_u8(msg, device_state.pixels[0].r);
    net_buf_simple_add_u8(msg, device_state.pixels[0].g);
    net_buf_simple_add_u8(msg, device_state.pixels[0].b);
    
    if (bt_mesh_model_send(model, ctx, msg, NULL, NULL)) {
        LOG_ERR("Failed to send LED status");
    }
}

static void keyasoftbox_led_set(struct bt_mesh_model *model,
                               struct bt_mesh_msg_ctx *ctx,
                               struct net_buf_simple *buf)
{
    uint8_t power = net_buf_simple_pull_u8(buf);
    uint8_t brightness = net_buf_simple_pull_u8(buf);
    uint8_t r = net_buf_simple_pull_u8(buf);
    uint8_t g = net_buf_simple_pull_u8(buf);
    uint8_t b = net_buf_simple_pull_u8(buf);
    
    LOG_INF("LED Set from 0x%04x: power=%d, bright=%d, RGB=(%d,%d,%d)", 
            ctx->addr, power, brightness, r, g, b);
    
    device_state.power_on = power;
    device_state.brightness = brightness;
    device_state.auto_mode = false;  // Stop animation when manually controlled
    
    k_work_cancel_delayable(&animation_work);
    
    set_all_pixels(r, g, b);
    update_led_strip();
    
    // Send status response
    keyasoftbox_led_get(model, ctx, buf);
}

/* GATT Characteristic Handlers */
static ssize_t read_device_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    char info[128];
    snprintf(info, sizeof(info),
             "{\"device\":\"KeyaSoftBox\",\"mesh_addr\":\"0x%04x\",\"provisioned\":%s,\"pixels\":%d}",
             device_state.mesh_addr,
             device_state.mesh_provisioned ? "true" : "false",
             NUM_PIXELS);
    
    return bt_gatt_attr_read(conn, attr, buf, len, offset, info, strlen(info));
}

static ssize_t read_led_status(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              void *buf, uint16_t len, uint16_t offset)
{
    char status[256];
    snprintf(status, sizeof(status),
             "{\"power\":%s,\"brightness\":%d,\"auto_mode\":%s,\"animation\":%d,\"speed\":%u}",
             device_state.power_on ? "true" : "false",
             device_state.brightness,
             device_state.auto_mode ? "true" : "false",
             device_state.animation_type,
             device_state.animation_speed);
    
    return bt_gatt_attr_read(conn, attr, buf, len, offset, status, strlen(status));
}

static ssize_t write_led_control(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset,
                                uint8_t flags)
{
    const char *data = (const char *)buf;
    char cmd[64];
    
    if (len == 0 || len >= sizeof(cmd)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    
    memcpy(cmd, data, len);
    cmd[len] = '\0';
    
    LOG_INF("LED Control command: %s", cmd);
    
    // Parse JSON-like commands
    if (strstr(cmd, "\"power\":true")) {
        device_state.power_on = true;
    } else if (strstr(cmd, "\"power\":false")) {
        device_state.power_on = false;
    }
    
    // Parse brightness
    char *bright_pos = strstr(cmd, "\"brightness\":");
    if (bright_pos) {
        device_state.brightness = atoi(bright_pos + 13);
    }
    
    // Parse RGB color
    char *r_pos = strstr(cmd, "\"r\":");
    char *g_pos = strstr(cmd, "\"g\":");
    char *b_pos = strstr(cmd, "\"b\":");
    
    if (r_pos && g_pos && b_pos) {
        uint8_t r = atoi(r_pos + 4);
        uint8_t g = atoi(g_pos + 4);
        uint8_t b = atoi(b_pos + 4);
        
        device_state.auto_mode = false;
        k_work_cancel_delayable(&animation_work);
        set_all_pixels(r, g, b);
    }
    
    // Parse animation
    if (strstr(cmd, "\"auto_mode\":true")) {
        device_state.auto_mode = true;
        char *anim_pos = strstr(cmd, "\"animation\":");
        if (anim_pos) {
            device_state.animation_type = atoi(anim_pos + 12);
        }
        char *speed_pos = strstr(cmd, "\"speed\":");
        if (speed_pos) {
            device_state.animation_speed = atoi(speed_pos + 8);
        }
        k_work_reschedule(&animation_work, K_MSEC(100));
    } else if (strstr(cmd, "\"auto_mode\":false")) {
        device_state.auto_mode = false;
        k_work_cancel_delayable(&animation_work);
    }
    
    update_led_strip();
    
    return len;
}

static ssize_t write_mesh_control(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len, uint16_t offset,
                                 uint8_t flags)
{
    if (len == 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    
    LOG_INF("Mesh control command received, len=%d", len);
    
    // Send mesh message to all devices
    if (device_state.mesh_provisioned) {
        mesh_led_control((uint8_t *)buf, len);
    }
    
    return len;
}

/* GATT Service Definition */
BT_GATT_SERVICE_DEFINE(keyasoftbox_svc,
    BT_GATT_PRIMARY_SERVICE(&keyasoftbox_service_uuid),
    
    BT_GATT_CHARACTERISTIC(&led_control_char_uuid.uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                          BT_GATT_PERM_WRITE, NULL, write_led_control, NULL),
                          
    BT_GATT_CHARACTERISTIC(&led_status_char_uuid.uuid,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ, read_led_status, NULL, NULL),
                          
    BT_GATT_CHARACTERISTIC(&mesh_control_char_uuid.uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                          BT_GATT_PERM_WRITE, NULL, write_mesh_control, NULL),
                          
    BT_GATT_CHARACTERISTIC(&device_info_char_uuid.uuid,
                          BT_GATT_CHRC_READ,
                          BT_GATT_PERM_READ, read_device_info, NULL, NULL),
);

/* Mesh Control Function */
static void mesh_led_control(uint8_t *data, uint16_t len)
{
    if (!device_state.mesh_provisioned) {
        LOG_WRN("Device not provisioned, cannot send mesh message");
        return;
    }
    
    struct bt_mesh_model *model = &root_models[2];
    struct net_buf_simple *msg = NET_BUF_SIMPLE(2 + len);
    
    bt_mesh_model_msg_init(msg, KEYASOFTBOX_LED_SET_OP);
    
    // Parse and forward the control data
    const char *cmd_data = (const char *)data;
    
    // Extract basic control parameters
    bool power = strstr(cmd_data, "\"power\":true") != NULL;
    uint8_t brightness = 255;
    uint8_t r = 255, g = 255, b = 255;
    
    char *bright_pos = strstr(cmd_data, "\"brightness\":");
    if (bright_pos) brightness = atoi(bright_pos + 13);
    
    char *r_pos = strstr(cmd_data, "\"r\":");
    char *g_pos = strstr(cmd_data, "\"g\":");
    char *b_pos = strstr(cmd_data, "\"b\":");
    
    if (r_pos) r = atoi(r_pos + 4);
    if (g_pos) g = atoi(g_pos + 4);
    if (b_pos) b = atoi(b_pos + 4);
    
    net_buf_simple_add_u8(msg, power ? 1 : 0);
    net_buf_simple_add_u8(msg, brightness);
    net_buf_simple_add_u8(msg, r);
    net_buf_simple_add_u8(msg, g);
    net_buf_simple_add_u8(msg, b);
    
    struct bt_mesh_msg_ctx ctx = {
        .net_idx = device_state.net_idx,
        .app_idx = device_state.app_idx,
        .addr = BT_MESH_ADDR_ALL_NODES,
        .send_ttl = BT_MESH_TTL_DEFAULT,
    };
    
    if (bt_mesh_model_send(model, &ctx, msg, NULL, NULL)) {
        LOG_ERR("Failed to send mesh LED control message");
    } else {
        LOG_INF("Mesh LED control message sent to all nodes");
    }
}

/* Button Handler */
static void button_pressed(const struct device *dev, struct gpio_callback *cb,
                          uint32_t pins)
{
    static int color_index = 0;
    const struct led_rgb colors[] = {
        {255, 0, 0},   // Red
        {0, 255, 0},   // Green
        {0, 0, 255},   // Blue
        {255, 255, 0}, // Yellow
        {255, 0, 255}, // Magenta
        {0, 255, 255}, // Cyan
        {255, 255, 255}, // White
        {0, 0, 0}      // Off
    };
    
    LOG_INF("Button pressed - changing color");
    
    device_state.auto_mode = false;
    k_work_cancel_delayable(&animation_work);
    
    struct led_rgb color = colors[color_index];
    set_all_pixels(color.r, color.g, color.b);
    update_led_strip();
    
    color_index = (color_index + 1) % ARRAY_SIZE(colors);
}

/* Provisioning Callbacks */
static void prov_complete(uint16_t net_idx, uint16_t addr)
{
    LOG_INF("Provisioning complete! Net IDX: 0x%04x, Addr: 0x%04x", net_idx, addr);
    
    device_state.mesh_provisioned = true;
    device_state.net_idx = net_idx;
    device_state.mesh_addr = addr;
    
    // Celebration animation
    for (int i = 0; i < 3; i++) {
        set_all_pixels(0, 255, 0);  // Green
        update_led_strip();
        k_sleep(K_MSEC(200));
        clear_all_pixels();
        update_led_strip();
        k_sleep(K_MSEC(200));
    }
}

static void prov_reset(void)
{
    LOG_INF("Provisioning reset");
    device_state.mesh_provisioned = false;
    device_state.mesh_addr = 0;
    device_state.net_idx = 0;
}

static int output_number(bt_mesh_output_action_t action, uint32_t number)
{
    LOG_INF("OOB Number: %u", number);
    
    // Display number by blinking LEDs
    clear_all_pixels();
    
    for (uint32_t i = 0; i < number && i < NUM_PIXELS; i++) {
        set_pixel_color(i, 0, 0, 255);  // Blue
    }
    update_led_strip();
    
    return 0;
}

/* BLE Advertisement */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_MESH_PROV_VAL)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, sizeof(DEVICE_NAME) - 1),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, KEYASOFTBOX_SERVICE_UUID),
};

/* Connection Callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err 0x%02x)", err);
        return;
    }
    LOG_INF("Connected to phone/controller");
    
    // Brief connection indication
    set_all_pixels(255, 255, 0);  // Yellow
    update_led_strip();
    k_sleep(K_MSEC(500));
    clear_all_pixels();
    update_led_strip();
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason 0x%02x)", reason);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* Initialization Functions */
static int gpio_init(void)
{
    int ret;
    
    if (!device_is_ready(button.port)) {
        LOG_ERR("Button GPIO device not ready");
        return -ENODEV;
    }
    
    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Error configuring button GPIO: %d", ret);
        return ret;
    }
    
    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Error configuring button interrupt: %d", ret);
        return ret;
    }
    
    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
    
    return 0;
}

static int led_strip_init(void)
{
    if (!device_is_ready(strip)) {
        LOG_ERR("LED strip device not ready");
        return -ENODEV;
    }
    
    LOG_INF("LED strip initialized with %d pixels", NUM_PIXELS);
    
    // Initial LED test
    set_all_pixels(50, 50, 50);  // Dim white
    update_led_strip();
    k_sleep(K_MSEC(1000));
    clear_all_pixels();
    update_led_strip();
    
    return 0;
}

static int mesh_init(void)
{
    int err;
    
    static struct bt_mesh_prov prov_handlers = {
        .uuid = prov.uuid,
        .output_size = prov.output_size,
        .output_actions = prov.output_actions,
        .output_number = output_number,
        .complete = prov_complete,
        .reset = prov_reset,
    };
    
    err = bt_mesh_init(&prov_handlers, &comp);
    if (err) {
        LOG_ERR("Mesh initialization failed (err %d)", err);
        return err;
    }
    
    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
        settings_load();
    }
    
    bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
    
    LOG_INF("Bluetooth mesh initialized");
    return 0;
}

/* Main Function */
int main(void)
{
    int err;
    
    LOG_INF("Starting KeyaSoftBox v1.0");
    
    // Initialize LED strip first
    err = led_strip_init();
    if (err) {
        LOG_ERR("LED strip initialization failed");
        return err;
    }
    
    // Initialize GPIO
    err = gpio_init();
    if (err) {
        LOG_ERR("GPIO initialization failed");
        return err;
    }
    
    // Initialize Bluetooth
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }
    
    LOG_INF("Bluetooth initialized");
    
    // Initialize Mesh
    err = mesh_init();
    if (err) {
        LOG_ERR("Mesh initialization failed");
        return err;
    }
    
    // Generate unique device UUID based on device ID
    uint32_t device_id = NRF_FICR->DEVICEID[0];
    memcpy(&dev_uuid[12], &device_id, 4);
    
    // Start advertising
    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad),
                         sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return err;
    }
    
    LOG_INF("KeyaSoftBox ready - advertising started");
    LOG_INF("Features: %d WS2812B LEDs, BLE Mesh, Phone Control", NUM_PIXELS);
    
    // Main loop
    while (1) {
        k_sleep(K_SECONDS(1));
        
        // Heartbeat for unprovisioned devices
        if (!device_state.mesh_provisioned) {
            static int heartbeat_counter = 0;
            if (++heartbeat_counter >= 10) {  // Every 10 seconds
                heartbeat_counter = 0;
                
                // Brief blue flash to indicate waiting for provisioning
                set_pixel_color(0, 0, 0, 100);  // First LED blue
                update_led_strip();
                k_sleep(K_MSEC(100));
                set_pixel_color(0, 0, 0, 0);
                update_led_strip();
            }
        }
    }
    
    return 0;
}