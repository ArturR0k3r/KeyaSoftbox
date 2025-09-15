#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include "ksb_common.h"

/**
 * Start web configuration server
 * @return 0 on success, negative error code on failure
 */
int web_config_start(void);

/**
 * Stop web configuration server
 */
void web_config_stop(void);

/**
 * Check if configuration has been received
 * @return true if configured, false otherwise
 */
bool web_config_is_configured(void);

/**
 * Get received configuration
 * @param config Pointer to store configuration
 * @return 0 on success, -ENOENT if no config available
 */
int web_config_get_config(struct ksb_network_config *config);

#endif // WEB_CONFIG_H