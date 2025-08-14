#include <zephyr/kernel.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "keyasoftbox.h"

LOG_MODULE_REGISTER(mesh_handler, LOG_LEVEL_INF);

/* External references */
extern struct keyasoftbox_state device_state;

/* Mesh message handling functions */
static void keyasoftbox_led_get(struct bt_mesh_model *model,
                               struct bt_mesh_msg_ctx *ctx,
                               struct net_buf_simple *buf);
static void keyasoftbox_led_set(struct bt_mesh_model *model,
                               struct bt_mesh_msg_ctx *ctx,
                               struct net_buf_simple *buf);

/* Model operations */
static const struct bt_mesh_model_op keyasoftbox_model_ops[] = {
    { KEYASOFTBOX_LED_GET_OP, 0, keyasoftbox_led_get },
    { KEYASOFTBOX_LED_SET_OP, 5, keyasoftbox_led_set },
    BT_MESH_MODEL_OP_END,
};

/* Model publication */
static struct bt_mesh_model_pub keyasoftbox_led_pub = {
    .msg = NET_BUF_SIMPLE(2 + 10), // Opcode + data
};

/* Get model reference from main.c */
extern struct bt_mesh_model *keyasoftbox_model;

/* Message Handlers */
static void keyasoftbox_led_get(struct bt_mesh_model *model,
                               struct bt_mesh_msg_ctx *ctx,
                               struct net_buf_simple *buf)
{
    struct net_buf_simple *msg = NET_BUF_SIMPLE(2 + 10);
    
    LOG_INF("LED Get request from 0x%04x", ctx->addr);
    
    bt_mesh_model_msg_init(msg, KEYASOFTBOX_LED_STATUS_OP);
    net_buf_simple_add_u8(msg, device_state.power_on ? 1 : 0);
    net_buf_simple_add_u8(msg, device_state.brightness);
    net_buf_simple_add_u8(msg, device_state.static_color.r);
    net_buf_simple_add_u8(msg, device_state.static_color.g);
    net_buf_simple_add_u8(msg, device_state.static_color.b);
    net_buf_simple_add_u8(msg, device_state.auto_mode ? 1 : 0);
    net_buf_simple_add_u8(msg, device_state.animation_type);
    net_buf_simple_add_le16(msg, device_state.animation_speed);
    
    if (bt_mesh_model_send(model, ctx, msg, NULL, NULL)) {
        LOG_ERR("Failed to send LED status response");
    }
}

static void keyasoftbox_led_set(struct bt_mesh_model *model,
                               struct bt_mesh_msg_ctx *ctx,
                               struct net_buf_simple *buf)
{
    uint8_t power, brightness, r, g, b;
    
    if (buf->len < 5) {
        LOG_ERR("Invalid LED set message length: %d", buf->len);
        return;
    }
    
    power = net_buf_simple_pull_u8(buf);
    brightness = net_buf_simple_pull_u8(buf);
    r = net_buf_simple_pull_u8(buf);
    g = net_buf_simple_pull_u8(buf);
    b = net_buf_simple_pull_u8(buf);
    
    LOG_INF("LED Set from 0x%04x: power=%d, bright=%d, RGB=(%d,%d,%d)", 
            ctx->addr, power, brightness, r, g, b);
    
    /* Update device state */
    device_state.power_on = (power != 0);
    device_state.brightness = brightness;
    device_state.static_color.r = r;
    device_state.static_color.g = g;
    device_state.static_color.b = b;
    
    /* Stop any running animation and set static color */
    device_state.auto_mode = false;
    k_work_cancel_delayable(&animation_work);
    
    /* Apply the color */
    if (device_state.power_on) {
        set_all_pixels(r, g, b);
    } else {
        clear_all_pixels();
    }
    update_led_strip();
    
    /* Send status response if requested */
    if (ctx->recv_op == KEYASOFTBOX_LED_SET_OP) {
        keyasoftbox_led_get(model, ctx, buf);
    }
    
    /* Notify connected phone about the change */
    notify_led_status_change();
}

/* Mesh Control Functions */
void mesh_send_led_command(uint8_t power, uint8_t brightness, uint8_t r, uint8_t g, uint8_t b)
{
    if (!device_state.mesh_provisioned) {
        LOG_WRN("Device not provisioned - cannot send mesh command");
        return;
    }
    
    struct bt_mesh_model *model = keyasoftbox_model;
    if (!model) {
        LOG_ERR("KeyaSoftBox model not found");
        return;
    }
    
    struct net_buf_simple *msg = NET_BUF_SIMPLE(2 + 5);
    
    bt_mesh_model_msg_init(msg, KEYASOFTBOX_LED_SET_OP);
    net_buf_simple_add_u8(msg, power);
    net_buf_simple_add_u8(msg, brightness);
    net_buf_simple_add_u8(msg, r);
    net_buf_simple_add_u8(msg, g);
    net_buf_simple_add_u8(msg, b);
    
    /* Send to all nodes in the network */
    struct bt_mesh_msg_ctx ctx = {
        .net_idx = device_state.net_idx,
        .app_idx = device_state.app_idx,
        .addr = BT_MESH_ADDR_ALL_NODES,
        .send_ttl = BT_MESH_TTL_DEFAULT,
    };
    
    int err = bt_mesh_model_send(model, &ctx, msg, NULL, NULL);
    if (err) {
        LOG_ERR("Failed to send mesh LED command: %d", err);
    } else {
        LOG_INF("Mesh LED command sent to all nodes");
    }
}

void mesh_send_effect_command(uint8_t effect_type, uint32_t speed)
{
    if (!device_state.mesh_provisioned) {
        LOG_WRN("Device not provisioned - cannot send mesh command");
        return;
    }
    
    /* For now, we'll encode effect commands as special LED set messages */
    /* This is a simplified approach - in a full implementation, you'd want */
    /* separate opcodes for effect control */
    
    LOG_INF("Sending effect command: type=%d, speed=%u", effect_type, speed);
    
    /* Use a special encoding where all RGB values are 255 to indicate effect mode */
    /* The effect type is encoded in the brightness field */
    mesh_send_led_command(1, effect_type, 255, 255, 255);
}

/* Mesh event handlers */
static void mesh_provisioned(uint16_t net_idx, uint16_t addr)
{
    LOG_INF("Mesh provisioned: net_idx=0x%04x, addr=0x%04x", net_idx, addr);
    
    device_state.mesh_provisioned = true;
    device_state.net_idx = net_idx;
    device_state.mesh_addr = addr;
    
    /* Visual indication of successful provisioning */
    for (int i = 0; i < 5; i++) {
        set_all_pixels(0, 255, 0);  /* Green */
        update_led_strip();
        k_sleep(K_MSEC(200));
        clear_all_pixels();
        update_led_strip();
        k_sleep(K_MSEC(200));
    }
}

static void mesh_unprovisioned(void)
{
    LOG_INF("Mesh unprovisioned");
    
    device_state.mesh_provisioned = false;
    device_state.net_idx = 0;
    device_state.mesh_addr = 0;
    
    /* Visual indication of unprovisioning */
    for (int i = 0; i < 3; i++) {
        set_all_pixels(255, 0, 0);  /* Red */
        update_led_strip();
        k_sleep(K_MSEC(300));
        clear_all_pixels();
        update_led_strip();
        k_sleep(K_MSEC(300));
    }
}

static void mesh_prov_complete(uint16_t net_idx, uint16_t addr)
{
    mesh_provisioned(net_idx, addr);
}

static void mesh_prov_reset(void)
{
    mesh_unprovisioned();
}

static int mesh_output_number(bt_mesh_output_action_t action, uint32_t number)
{
    LOG_INF("OOB Number: %u", number);
    
    /* Display the number using LED blinks */
    clear_all_pixels();
    update_led_strip();
    k_sleep(K_MSEC(500));
    
    /* Show number of blinks equal to the OOB number (mod 10) */
    uint8_t blinks = number % 10;
    if (blinks == 0) blinks = 10;
    
    for (int i = 0; i < blinks; i++) {
        set_all_pixels(0, 0, 255);  /* Blue */
        update_led_strip();
        k_sleep(K_MSEC(300));
        clear_all_pixels();
        update_led_strip();
        k_sleep(K_MSEC(300));
    }
    
    return 0;
}

static int mesh_input_number(bt_mesh_input_action_t action, uint8_t size)
{
    LOG_INF("Input number of size %u requested", size);
    /* In a real implementation, you might use button presses to input the number */
    return 0;
}

static void mesh_input_complete(void)
{
    LOG_INF("Input complete");
}

/* Health Server Callbacks */
static void attention_on(struct bt_mesh_model *model)
{
    LOG_INF("Attention ON");
    device_state.auto_mode = true;
    device_state.animation_type = EFFECT_RAINBOW_CYCLE;
    device_state.animation_speed = 100;
    k_work_reschedule(&animation_work, K_MSEC(device_state.animation_speed));
}

static void attention_off(struct bt_mesh_model *model)
{
    LOG_INF("Attention OFF");
    device_state.auto_mode = false;
    k_work_cancel_delayable(&animation_work);
    clear_all_pixels();
    update_led_strip();
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
    .attn_on = attention_on,
    .attn_off = attention_off,
};

/* Model definitions for export */
const struct bt_mesh_model_op keyasoftbox_model_op[] = {
    { KEYASOFTBOX_LED_GET_OP, 0, keyasoftbox_led_get },
    { KEYASOFTBOX_LED_SET_OP, 5, keyasoftbox_led_set },
    BT_MESH_MODEL_OP_END,
};

/* Provisioning structure */
static const struct bt_mesh_prov mesh_prov = {
    .uuid = NULL, /* Will be set dynamically */
    .output_size = 4,
    .output_actions = BT_MESH_DISPLAY_NUMBER | BT_MESH_BLINK,
    .output_number = mesh_output_number,
    .input_size = 4,
    .input_actions = BT_MESH_ENTER_NUMBER,
    .input_number = mesh_input_number,
    .input_complete = mesh_input_complete,
    .complete = mesh_prov_complete,
    .reset = mesh_prov_reset,
};

/* Mesh initialization */
int mesh_init_keyasoftbox(void)
{
    int err;
    
    /* Initialize mesh with our provisioning callbacks */
    struct bt_mesh_prov prov = mesh_prov;
    
    /* Generate a unique UUID based on device ID if available */
    static uint8_t dev_uuid[16] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22,
                                   0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00 };
    
    /* Try to make UUID more unique using system info */
#ifdef CONFIG_HWINFO
    uint8_t hwid[16];
    size_t hwid_len = sizeof(hwid);
    if (hwinfo_get_device_id(hwid, &hwid_len) == 0 && hwid_len >= 8) {
        memcpy(&dev_uuid[8], hwid, MIN(8, hwid_len));
    }
#endif
    
    prov.uuid = dev_uuid;
    
    /* Get composition and models from main */
    extern const struct bt_mesh_comp comp;
    
    err = bt_mesh_init(&prov, &comp);
    if (err) {
        LOG_ERR("Mesh initialization failed: %d", err);
        return err;
    }
    
    /* Enable provisioning */
    bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
    
    LOG_INF("Mesh handler initialized successfully");
    return 0;
}