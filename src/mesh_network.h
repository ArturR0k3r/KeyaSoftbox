#ifndef MESH_NETWORK_H
#define MESH_NETWORK_H

#include "ksb_common.h"

/**
 * Initialize mesh networking subsystem
 * @param network_name Name of the mesh network
 * @return 0 on success, negative error code on failure
 */
int mesh_network_init(const char *network_name);

/**
 * Scan for existing mesh networks
 * @param timeout_ms Timeout in milliseconds
 * @return 0 if network found, -ENOENT if not found, other negative on error
 */
int mesh_network_scan(uint32_t timeout_ms);

/**
 * Join an existing mesh network as client
 * @return 0 on success, negative error code on failure
 */
int mesh_network_join(void);

/**
 * Create a new mesh network as master
 * @return 0 on success, negative error code on failure
 */
int mesh_network_create(void);

/**
 * Check if mesh network is connected
 * @return true if connected, false otherwise
 */
bool mesh_network_is_connected(void);

/**
 * Broadcast LED command to all mesh nodes
 * @param cmd LED command to broadcast
 * @return 0 on success, negative error code on failure
 */
int mesh_broadcast_led_command(struct ksb_led_command *cmd);

/**
 * Process mesh network operations (called periodically)
 */
void mesh_network_process(void);

/**
 * Reset and cleanup mesh network
 */
void mesh_network_reset(void);

#endif // MESH_NETWORK_H