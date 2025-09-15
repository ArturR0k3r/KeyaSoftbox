#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/posix/unistd.h>
#include "ksb_common.h"
#include "mesh_network.h"
#include "led_control.h"

LOG_MODULE_REGISTER(mesh_network, CONFIG_LOG_DEFAULT_LEVEL);

static struct mesh_context
{
    char network_name[KSB_MAX_NETWORK_NAME_LEN];
    bool is_connected;
    bool is_master;
    int mesh_socket;
    struct sockaddr_in mesh_addr;
    uint8_t node_id;
    uint8_t master_node_id;
    struct k_thread rx_thread;
    K_KERNEL_STACK_MEMBER(rx_stack, 2048);
} mesh_ctx;

// WiFi management
static struct net_mgmt_event_callback wifi_cb;
static struct k_sem wifi_connected;

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint32_t mgmt_event, struct net_if *iface)
{
    switch (mgmt_event)
    {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        LOG_INF("WiFi connected");
        k_sem_give(&wifi_connected);
        break;
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        LOG_INF("WiFi disconnected");
        mesh_ctx.is_connected = false;
        break;
    default:
        break;
    }
}

static int wifi_connect(const char *ssid, const char *password)
{
    struct net_if *iface = net_if_get_default();
    struct wifi_connect_req_params wifi_params = {
        .ssid = ssid,
        .ssid_length = strlen(ssid),
        .psk = password,
        .psk_length = strlen(password),
        .channel = WIFI_CHANNEL_ANY,
        .security = WIFI_SECURITY_TYPE_PSK,
    };

    if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &wifi_params,
                 sizeof(struct wifi_connect_req_params)))
    {
        LOG_ERR("WiFi connection failed");
        return -EIO;
    }

    // Wait for connection with timeout
    if (k_sem_take(&wifi_connected, K_MSEC(30000)) != 0)
    {
        LOG_ERR("WiFi connection timeout");
        return -ETIMEDOUT;
    }

    return 0;
}

// Mesh networking
static void mesh_rx_thread(void *arg1, void *arg2, void *arg3)
{
    int ret;
    struct sockaddr_in src_addr;
    socklen_t addrlen = sizeof(src_addr);
    struct ksb_led_command cmd;

    while (mesh_ctx.is_connected)
    {
        ret = recvfrom(mesh_ctx.mesh_socket, &cmd, sizeof(cmd), 0,
                       (struct sockaddr *)&src_addr, &addrlen);

        if (ret == sizeof(cmd))
        {
            LOG_DBG("Received LED command: pattern=%d", cmd.pattern);

            // Apply LED command locally
            led_control_set_pattern(cmd.pattern, cmd.color, cmd.brightness, cmd.speed);

            // If we're master, forward to other nodes
            if (mesh_ctx.is_master)
            {
                mesh_broadcast_led_command(&cmd);
            }
        }
        else if (ret < 0 && errno != EAGAIN)
        {
            LOG_ERR("Mesh receive error: %d", errno);
            break;
        }

        k_msleep(10);
    }
}

int mesh_network_init(const char *network_name)
{
    strncpy(mesh_ctx.network_name, network_name, sizeof(mesh_ctx.network_name) - 1);
    mesh_ctx.network_name[sizeof(mesh_ctx.network_name) - 1] = '\0';

    mesh_ctx.is_connected = false;
    mesh_ctx.is_master = false;
    mesh_ctx.node_id = g_ksb_ctx.config.device_id;

    // Initialize WiFi callbacks
    k_sem_init(&wifi_connected, 0, 1);
    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT |
                                     NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    LOG_INF("Mesh network initialized for: %s", mesh_ctx.network_name);
    return 0;
}

int mesh_network_scan(uint32_t timeout_ms)
{
    // Try to connect to existing mesh network
    char mesh_ssid[64];
    snprintf(mesh_ssid, sizeof(mesh_ssid), "KSB_MESH_%s", mesh_ctx.network_name);

    LOG_INF("Scanning for mesh network: %s", mesh_ssid);

    int ret = wifi_connect(mesh_ssid, "keya_mesh_2024");
    if (ret == 0)
    {
        return 0; // Found and connected to existing mesh
    }

    LOG_INF("No existing mesh network found");
    return -ENOENT;
}

int mesh_network_join(void)
{
    int ret;

    LOG_INF("Joining mesh network as client");

    // Create UDP socket for mesh communication
    mesh_ctx.mesh_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (mesh_ctx.mesh_socket < 0)
    {
        LOG_ERR("Failed to create mesh socket");
        return -errno;
    }

    // Configure socket for broadcast
    int broadcast = 1;
    ret = setsockopt(mesh_ctx.mesh_socket, SOL_SOCKET, SO_BROADCAST,
                     &broadcast, sizeof(broadcast));
    if (ret < 0)
    {
        LOG_ERR("Failed to set broadcast option");
        close(mesh_ctx.mesh_socket);
        return -errno;
    }

    // Bind socket
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(KSB_MESH_PORT),
        .sin_addr.s_addr = INADDR_ANY};

    ret = bind(mesh_ctx.mesh_socket, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    if (ret < 0)
    {
        LOG_ERR("Failed to bind mesh socket");
        close(mesh_ctx.mesh_socket);
        return -errno;
    }

    // Set up broadcast address
    mesh_ctx.mesh_addr.sin_family = AF_INET;
    mesh_ctx.mesh_addr.sin_port = htons(KSB_MESH_PORT);
    mesh_ctx.mesh_addr.sin_addr.s_addr = INADDR_BROADCAST;

    mesh_ctx.is_connected = true;
    mesh_ctx.is_master = false;

    // Start receive thread
    k_thread_create(&mesh_ctx.rx_thread, mesh_ctx.rx_stack,
                    K_KERNEL_STACK_SIZEOF(mesh_ctx.rx_stack),
                    mesh_rx_thread, NULL, NULL, NULL,
                    6, 0, K_NO_WAIT);
    k_thread_name_set(&mesh_ctx.rx_thread, "mesh_rx");

    LOG_INF("Joined mesh network successfully");
    return 0;
}

int mesh_network_create(void)
{
    int ret;
    struct net_if *iface = net_if_get_default();

    LOG_INF("Creating mesh network as master");

    // Start WiFi access point
    char ap_ssid[64];
    snprintf(ap_ssid, sizeof(ap_ssid), "KSB_MESH_%s", mesh_ctx.network_name);

    struct wifi_connect_req_params ap_params = {
        .ssid = ap_ssid,
        .ssid_length = strlen(ap_ssid),
        .psk = "keya_mesh_2024",
        .psk_length = strlen("keya_mesh_2024"),
        .channel = 6,
        .security = WIFI_SECURITY_TYPE_PSK,
    };

    ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap_params,
                   sizeof(struct wifi_connect_req_params));
    if (ret)
    {
        LOG_ERR("Failed to start WiFi AP: %d", ret);
        return ret;
    }

    // Wait a bit for AP to start
    k_msleep(2000);

    // Create UDP socket for mesh communication
    mesh_ctx.mesh_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (mesh_ctx.mesh_socket < 0)
    {
        LOG_ERR("Failed to create mesh socket");
        return -errno;
    }

    // Configure socket for broadcast
    int broadcast = 1;
    ret = setsockopt(mesh_ctx.mesh_socket, SOL_SOCKET, SO_BROADCAST,
                     &broadcast, sizeof(broadcast));
    if (ret < 0)
    {
        LOG_ERR("Failed to set broadcast option");
        close(mesh_ctx.mesh_socket);
        return -errno;
    }

    // Bind socket
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(KSB_MESH_PORT),
        .sin_addr.s_addr = INADDR_ANY};

    ret = bind(mesh_ctx.mesh_socket, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    if (ret < 0)
    {
        LOG_ERR("Failed to bind mesh socket");
        close(mesh_ctx.mesh_socket);
        return -errno;
    }

    // Set up broadcast address
    mesh_ctx.mesh_addr.sin_family = AF_INET;
    mesh_ctx.mesh_addr.sin_port = htons(KSB_MESH_PORT);
    mesh_ctx.mesh_addr.sin_addr.s_addr = INADDR_BROADCAST;

    mesh_ctx.is_connected = true;
    mesh_ctx.is_master = true;
    mesh_ctx.master_node_id = mesh_ctx.node_id;

    // Start receive thread
    k_thread_create(&mesh_ctx.rx_thread, mesh_ctx.rx_stack,
                    K_KERNEL_STACK_SIZEOF(mesh_ctx.rx_stack),
                    mesh_rx_thread, NULL, NULL, NULL,
                    6, 0, K_NO_WAIT);
    k_thread_name_set(&mesh_ctx.rx_thread, "mesh_rx");

    LOG_INF("Created mesh network successfully: %s", ap_ssid);
    return 0;
}

bool mesh_network_is_connected(void)
{
    return mesh_ctx.is_connected;
}

int mesh_broadcast_led_command(struct ksb_led_command *cmd)
{
    if (!mesh_ctx.is_connected)
    {
        return -ENOTCONN;
    }

    int ret = sendto(mesh_ctx.mesh_socket, cmd, sizeof(*cmd), 0,
                     (struct sockaddr *)&mesh_ctx.mesh_addr,
                     sizeof(mesh_ctx.mesh_addr));

    if (ret < 0)
    {
        LOG_ERR("Failed to broadcast LED command: %d", errno);
        return -errno;
    }

    LOG_DBG("Broadcasted LED command: pattern=%d", cmd->pattern);
    return 0;
}

void mesh_network_process(void)
{
    // Process any pending mesh operations
    // This is called periodically from the main state machine

    // Check connection health
    if (mesh_ctx.is_connected)
    {
        // Could add ping/heartbeat mechanism here
    }
}

void mesh_network_reset(void)
{
    LOG_INF("Resetting mesh network");

    mesh_ctx.is_connected = false;

    if (mesh_ctx.mesh_socket >= 0)
    {
        close(mesh_ctx.mesh_socket);
        mesh_ctx.mesh_socket = -1;
    }

    // Terminate receive thread
    k_thread_abort(&mesh_ctx.rx_thread);

    // Disconnect WiFi
    struct net_if *iface = net_if_get_default();
    if (mesh_ctx.is_master)
    {
        net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);
    }
    else
    {
        net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
    }
}