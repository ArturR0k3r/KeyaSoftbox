#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include "ksb_common.h"

/**
 * Initialize NVS storage subsystem
 * @return 0 on success, negative error code on failure
 */
int nvs_storage_init(void);

/**
 * Save configuration to NVS
 * @param config Configuration to save
 * @return 0 on success, negative error code on failure
 */
int nvs_storage_save_config(const struct ksb_network_config *config);

/**
 * Load configuration from NVS
 * @param config Pointer to store loaded configuration
 * @return 0 on success, negative error code on failure
 */
int nvs_storage_load_config(struct ksb_network_config *config);

/**
 * Clear configuration from NVS
 * @return 0 on success, negative error code on failure
 */
int nvs_storage_clear_config(void);

#endif // NVS_STORAGE_H