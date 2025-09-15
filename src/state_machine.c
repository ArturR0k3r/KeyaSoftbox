#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "ksb_common.h"
#include "state_machine.h"
#include "web_config.h"
#include "mesh_network.h"
#include "led_control.h"
#include "nvs_storage.h"

LOG_MODULE_REGISTER(state_machine, CONFIG_LOG_DEFAULT_LEVEL);

static void state_machine_thread(void);
K_THREAD_DEFINE(state_machine_tid, 4096, state_machine_thread, NULL, NULL, NULL, 5, 0, 0);

static void transition_to_state(enum ksb_system_state new_state)
{
    k_sem_take(&g_ksb_ctx.state_lock, K_FOREVER);

    if (g_ksb_ctx.current_state != new_state)
    {
        LOG_INF("State transition: %d -> %d", g_ksb_ctx.current_state, new_state);
        g_ksb_ctx.current_state = new_state;
    }

    k_sem_give(&g_ksb_ctx.state_lock);
}

static void handle_system_init(void)
{
    LOG_INF("System initialization complete");

    if (g_ksb_ctx.config.is_configured)
    {
        transition_to_state(KSB_STATE_NETWORK_SCAN);
    }
    else
    {
        transition_to_state(KSB_STATE_CONFIG_MODE);
    }
}

static void handle_config_mode(void)
{
    int ret;

    LOG_INF("Entering configuration mode");

    // Start web configuration server
    ret = web_config_start();
    if (ret != 0)
    {
        LOG_ERR("Failed to start web config: %d", ret);
        transition_to_state(KSB_STATE_ERROR_RECOVERY);
        return;
    }

    // Wait for configuration or timeout
    uint32_t start_time = k_uptime_get_32();
    while (g_ksb_ctx.current_state == KSB_STATE_CONFIG_MODE)
    {

        if (web_config_is_configured())
        {
            // Configuration received
            struct ksb_network_config new_config;
            if (web_config_get_config(&new_config) == 0)
            {
                g_ksb_ctx.config = new_config;
                nvs_storage_save_config(&g_ksb_ctx.config);
                LOG_INF("Configuration saved: %s", g_ksb_ctx.config.network_name);
                web_config_stop();
                transition_to_state(KSB_STATE_NETWORK_SCAN);
                break;
            }
        }

        // Check timeout (5 minutes)
        if (k_uptime_get_32() - start_time > 300000)
        {
            LOG_WRN("Configuration timeout");
            web_config_stop();
            transition_to_state(KSB_STATE_ERROR_RECOVERY);
            break;
        }

        k_msleep(1000);
    }
}

static void handle_network_scan(void)
{
    int ret;

    LOG_INF("Scanning for mesh network: %s", g_ksb_ctx.config.network_name);

    ret = mesh_network_init(g_ksb_ctx.config.network_name);
    if (ret != 0)
    {
        LOG_ERR("Failed to initialize mesh network: %d", ret);
        transition_to_state(KSB_STATE_ERROR_RECOVERY);
        return;
    }

    // Try to find existing mesh network (30 second timeout)
    ret = mesh_network_scan(30000);
    if (ret == 0)
    {
        // Found existing network
        LOG_INF("Found existing mesh network");
        transition_to_state(KSB_STATE_MESH_CLIENT);
    }
    else
    {
        // No network found, become master
        LOG_INF("No existing network found, becoming master");
        transition_to_state(KSB_STATE_MESH_MASTER);
    }
}

static void handle_mesh_client(void)
{
    int ret;

    LOG_INF("Joining mesh network as client");

    ret = mesh_network_join();
    if (ret == 0)
    {
        LOG_INF("Successfully joined mesh network");
        transition_to_state(KSB_STATE_OPERATIONAL);
    }
    else
    {
        LOG_ERR("Failed to join mesh network: %d", ret);
        transition_to_state(KSB_STATE_ERROR_RECOVERY);
    }
}

static void handle_mesh_master(void)
{
    int ret;

    LOG_INF("Creating mesh network as master");

    ret = mesh_network_create();
    if (ret == 0)
    {
        LOG_INF("Successfully created mesh network");
        transition_to_state(KSB_STATE_OPERATIONAL);
    }
    else
    {
        LOG_ERR("Failed to create mesh network: %d", ret);
        transition_to_state(KSB_STATE_ERROR_RECOVERY);
    }
}

static void handle_operational(void)
{
    static bool first_time = true;

    if (first_time)
    {
        LOG_INF("System operational");

        // Start LED patterns
        struct led_rgb default_color = {100, 100, 100}; // White
        led_control_set_pattern(KSB_PATTERN_BREATHING, default_color, 128, 100);

        first_time = false;
    }

    // Check mesh network status
    if (!mesh_network_is_connected())
    {
        LOG_WRN("Mesh network connection lost");
        transition_to_state(KSB_STATE_CONNECTION_LOST);
        return;
    }

    // Process mesh messages
    mesh_network_process();

    k_msleep(100);
}

static void handle_connection_lost(void)
{
    LOG_INF("Handling connection loss");

    // Stop LED patterns
    led_control_set_pattern(KSB_PATTERN_OFF, (struct led_rgb){0, 0, 0}, 0, 0);

    // Try to reconnect
    transition_to_state(KSB_STATE_ERROR_RECOVERY);
}

static void handle_error_recovery(void)
{
    static int retry_count = 0;

    LOG_INF("Error recovery attempt %d", retry_count + 1);

    // Reset network
    mesh_network_reset();

    // Wait before retry
    k_msleep(5000);

    retry_count++;
    if (retry_count < 3)
    {
        // Retry network scan
        transition_to_state(KSB_STATE_NETWORK_SCAN);
    }
    else
    {
        // Too many retries, go back to config mode
        LOG_WRN("Too many recovery attempts, returning to config mode");
        retry_count = 0;
        g_ksb_ctx.config.is_configured = false;
        transition_to_state(KSB_STATE_CONFIG_MODE);
    }
}

static void state_machine_thread(void)
{
    LOG_INF("State machine thread started");

    while (g_ksb_ctx.system_running)
    {
        switch (g_ksb_ctx.current_state)
        {
        case KSB_STATE_SYSTEM_INIT:
            handle_system_init();
            break;

        case KSB_STATE_CONFIG_MODE:
            handle_config_mode();
            break;

        case KSB_STATE_NETWORK_SCAN:
            handle_network_scan();
            break;

        case KSB_STATE_MESH_CLIENT:
            handle_mesh_client();
            break;

        case KSB_STATE_MESH_MASTER:
            handle_mesh_master();
            break;

        case KSB_STATE_OPERATIONAL:
            handle_operational();
            break;

        case KSB_STATE_CONNECTION_LOST:
            handle_connection_lost();
            break;

        case KSB_STATE_ERROR_RECOVERY:
            handle_error_recovery();
            break;

        default:
            LOG_ERR("Invalid state: %d", g_ksb_ctx.current_state);
            transition_to_state(KSB_STATE_ERROR_RECOVERY);
            break;
        }

        k_msleep(100);
    }
}

int state_machine_init(void)
{
    LOG_INF("State machine initialized");
    return 0;
}

void state_machine_start(void)
{
    LOG_INF("State machine started");
    // Thread starts automatically due to K_THREAD_DEFINE
}