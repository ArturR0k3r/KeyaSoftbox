#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/logging/log.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/random/random.h>

#include "ksb_common.h"
#include "web_config.h"

LOG_MODULE_REGISTER(web_config, CONFIG_LOG_DEFAULT_LEVEL);

static struct web_config_context
{
    bool server_running;
    bool config_received;
    struct ksb_network_config received_config;
    struct k_thread server_thread;
    K_KERNEL_STACK_MEMBER(server_stack, 4096);
} web_ctx;

// HTML pages
static const char *index_html =
    "<!DOCTYPE html>\n"
    "<html><head>\n"
    "<title>KSB Configuration</title>\n"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>\n"
    "<style>\n"
    "body { font-family: Arial, sans-serif; margin: 40px; background: #f0f0f0; }\n"
    ".container { max-width: 400px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }\n"
    "h1 { color: #333; text-align: center; margin-bottom: 30px; }\n"
    "input[type=text] { width: 100%; padding: 12px; margin: 8px 0; border: 2px solid #ddd; border-radius: 4px; box-sizing: border-box; }\n"
    "input[type=submit] { width: 100%; background-color: #4CAF50; color: white; padding: 14px 20px; margin: 8px 0; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }\n"
    "input[type=submit]:hover { background-color: #45a049; }\n"
    ".info { background: #e7f3ff; padding: 15px; border-radius: 4px; margin: 20px 0; border-left: 4px solid #2196F3; }\n"
    "</style>\n"
    "</head><body>\n"
    "<div class='container'>\n"
    "<h1>🔗 KSB Setup</h1>\n"
    "<div class='info'>\n"
    "<strong>Keya-Soft-Box</strong><br>\n"
    "Version: " KSB_VERSION_STRING "<br>\n"
    "Configure your mesh lighting network name below.\n"
    "</div>\n"
    "<form action='/config' method='POST'>\n"
    "<label for='network'>Network Name:</label>\n"
    "<input type='text' id='network' name='network' placeholder='Living Room' maxlength='31' required>\n"
    "<input type='submit' value='Save Configuration'>\n"
    "</form>\n"
    "</div>\n"
    "</body></html>";

static const char *success_html =
    "<!DOCTYPE html>\n"
    "<html><head>\n"
    "<title>KSB Configuration</title>\n"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>\n"
    "<meta http-equiv='refresh' content='5;url=/'>\n"
    "<style>\n"
    "body { font-family: Arial, sans-serif; margin: 40px; background: #f0f0f0; }\n"
    ".container { max-width: 400px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); text-align: center; }\n"
    "h1 { color: #4CAF50; }\n"
    ".success { background: #d4edda; color: #155724; padding: 15px; border-radius: 4px; margin: 20px 0; border: 1px solid #c3e6cb; }\n"
    "</style>\n"
    "</head><body>\n"
    "<div class='container'>\n"
    "<h1>✅ Configuration Saved!</h1>\n"
    "<div class='success'>\n"
    "Your mesh network configuration has been saved.<br>\n"
    "The device will restart and begin networking.<br><br>\n"
    "<strong>Network:</strong> %s\n"
    "</div>\n"
    "<p>This page will redirect in 5 seconds...</p>\n"
    "</div>\n"
    "</body></html>";

// Web server thread
static void web_server_thread(void *arg1, void *arg2, void *arg3)
{
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[1024];
    int ret;

    // Create server socket
    server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock < 0)
    {
        LOG_ERR("Failed to create server socket: %d", errno);
        return;
    }

    // Set socket options
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(KSB_WEB_PORT);

    ret = bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0)
    {
        LOG_ERR("Failed to bind server socket: %d", errno);
        close(server_sock);
        return;
    }

    // Listen for connections
    ret = listen(server_sock, 2);
    if (ret < 0)
    {
        LOG_ERR("Failed to listen on server socket: %d", errno);
        close(server_sock);
        return;
    }

    LOG_INF("Web server listening on port %d", KSB_WEB_PORT);

    while (web_ctx.server_running)
    {
        // Accept client connection
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0)
        {
            if (errno != EAGAIN)
            {
                LOG_ERR("Accept failed: %d", errno);
            }
            k_msleep(100);
            continue;
        }

        LOG_DBG("Client connected");

        // Read HTTP request
        ret = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (ret > 0)
        {
            buffer[ret] = '\0';

            // Parse HTTP request (simple implementation)
            char method[10], path[64], version[16];
            if (sscanf(buffer, "%9s %63s %15s", method, path, version) == 3)
            {

                // Find request body for POST requests
                char *body = strstr(buffer, "\r\n\r\n");
                if (body)
                {
                    body += 4;
                }

                // Prepare response
                char response[2048];
                const char *response_body;
                const char *content_type = "text/html";

                if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0)
                {
                    response_body = index_html;
                }
                else if (strcmp(method, "POST") == 0 && strcmp(path, "/config") == 0)
                {
                    if (body && strlen(body) > 0)
                    {
                        // Parse network name from form data
                        char network_name[KSB_MAX_NETWORK_NAME_LEN] = {0};
                        const char *network_param = "network=";
                        char *start = strstr(body, network_param);

                        if (start)
                        {
                            start += strlen(network_param);
                            char *end = strchr(start, '&');
                            if (!end)
                                end = start + strlen(start);

                            int len = MIN(end - start, sizeof(network_name) - 1);
                            strncpy(network_name, start, len);

                            // URL decode
                            for (int i = 0; i < len; i++)
                            {
                                if (network_name[i] == '+')
                                {
                                    network_name[i] = ' ';
                                }
                                // Handle %20 etc. (basic implementation)
                                if (network_name[i] == '%' && i + 2 < len)
                                {
                                    char hex[3] = {network_name[i + 1], network_name[i + 2], 0};
                                    network_name[i] = (char)strtol(hex, NULL, 16);
                                    memmove(&network_name[i + 1], &network_name[i + 3], len - i - 2);
                                    len -= 2;
                                }
                            }

                            // Save configuration
                            strncpy(web_ctx.received_config.network_name, network_name,
                                    sizeof(web_ctx.received_config.network_name) - 1);
                            web_ctx.received_config.is_configured = true;
                            web_ctx.received_config.device_id = sys_rand32_get() & 0xFF;
                            web_ctx.config_received = true;

                            LOG_INF("Configuration received: %s", network_name);

                            snprintf(response, sizeof(response), success_html, network_name);
                            response_body = response;
                        }
                        else
                        {
                            response_body = "Invalid form data";
                            content_type = "text/plain";
                        }
                    }
                    else
                    {
                        response_body = "Missing form data";
                        content_type = "text/plain";
                    }
                }
                else
                {
                    response_body = "404 Not Found";
                    content_type = "text/plain";
                }

                // Send HTTP response
                char http_response[3072];
                snprintf(http_response, sizeof(http_response),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %d\r\n"
                         "Connection: close\r\n"
                         "\r\n"
                         "%s",
                         content_type, (int)strlen(response_body), response_body);

                send(client_sock, http_response, strlen(http_response), 0);
            }
        }

        close(client_sock);
    }

    close(server_sock);
    LOG_INF("Web server stopped");
}

int web_config_start(void)
{
    int ret;
    struct net_if *iface = net_if_get_default();

    LOG_INF("Starting web configuration server");

    // Generate unique AP SSID
    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X",
             KSB_AP_SSID_PREFIX,
             (g_ksb_ctx.config.device_id >> 4) & 0x0F,
             g_ksb_ctx.config.device_id & 0x0F);

    // Start WiFi access point
    struct wifi_connect_req_params ap_params = {
        .ssid = ap_ssid,
        .ssid_length = strlen(ap_ssid),
        .psk = KSB_AP_PASSWORD,
        .psk_length = strlen(KSB_AP_PASSWORD),
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

    LOG_INF("WiFi AP started: %s", ap_ssid);

    // Wait for AP to be ready
    k_msleep(3000);

    // Initialize web context
    web_ctx.server_running = true;
    web_ctx.config_received = false;
    memset(&web_ctx.received_config, 0, sizeof(web_ctx.received_config));

    // Start web server thread
    k_thread_create(&web_ctx.server_thread, web_ctx.server_stack,
                    K_KERNEL_STACK_SIZEOF(web_ctx.server_stack),
                    web_server_thread, NULL, NULL, NULL,
                    6, 0, K_NO_WAIT);
    k_thread_name_set(&web_ctx.server_thread, "web_server");

    LOG_INF("Web server started at http://192.168.4.1/");
    return 0;
}

void web_config_stop(void)
{
    struct net_if *iface = net_if_get_default();

    LOG_INF("Stopping web configuration server");

    web_ctx.server_running = false;

    // Terminate server thread
    k_thread_abort(&web_ctx.server_thread);

    // Disable WiFi AP
    net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);

    LOG_INF("Web configuration server stopped");
}

bool web_config_is_configured(void)
{
    return web_ctx.config_received;
}

int web_config_get_config(struct ksb_network_config *config)
{
    if (!web_ctx.config_received)
    {
        return -ENOENT;
    }

    *config = web_ctx.received_config;
    return 0;
}