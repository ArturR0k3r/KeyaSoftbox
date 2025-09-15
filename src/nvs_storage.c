#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/flash.h>
#include "ksb_common.h"
#include "nvs_storage.h"

LOG_MODULE_REGISTER(nvs_storage, CONFIG_LOG_DEFAULT_LEVEL);

#define NVS_PARTITION_ID FIXED_PARTITION_ID(storage_partition)
#define NVS_CONFIG_KEY 1

static struct nvs_fs nvs;

int nvs_storage_init(void)
{
    int ret;
    const struct flash_area *fa;

    // Open flash area for the storage partition
    ret = flash_area_open(NVS_PARTITION_ID, &fa);
    if (ret)
    {
        LOG_ERR("Failed to open flash area: %d", ret);
        return ret;
    }

    // Get flash device from flash area
    nvs.flash_device = flash_area_get_device(fa);
    if (!device_is_ready(nvs.flash_device))
    {
        LOG_ERR("Flash device not ready");
        flash_area_close(fa);
        return -ENODEV;
    }

    // Set up NVS parameters
    nvs.offset = fa->fa_off;

    // Get flash parameters - use flash_get_parameters instead of deprecated flash_get_page_info
    const struct flash_parameters *flash_params = flash_get_parameters(nvs.flash_device);
    if (!flash_params)
    {
        LOG_ERR("Unable to get flash parameters");
        flash_area_close(fa);
        return -EINVAL;
    }

    // Use write block size as sector size (typical for NVS)
    nvs.sector_size = flash_params->write_block_size;
    if (nvs.sector_size == 0)
    {
        // Fallback to erase block size if write block size is 0
        nvs.sector_size = flash_params->erase_value ? 4096 : 256; // Common defaults
    }

    // Calculate sector count based on partition size
    nvs.sector_count = fa->fa_size / nvs.sector_size;

    // Close flash area (we have what we need)
    flash_area_close(fa);

    // Ensure we have at least 2 sectors (NVS requirement)
    if (nvs.sector_count < 2)
    {
        LOG_ERR("NVS needs at least 2 sectors, got %d", nvs.sector_count);
        return -EINVAL;
    }

    ret = nvs_mount(&nvs);
    if (ret)
    {
        LOG_ERR("NVS mount failed: %d", ret);
        return ret;
    }

    LOG_INF("NVS storage initialized: %d sectors of %d bytes at offset 0x%x",
            nvs.sector_count, nvs.sector_size, nvs.offset);
    return 0;
}

int nvs_storage_save_config(const struct ksb_network_config *config)
{
    int ret = nvs_write(&nvs, NVS_CONFIG_KEY, config, sizeof(*config));
    if (ret < 0)
    {
        LOG_ERR("Failed to save config: %d", ret);
        return ret;
    }

    LOG_INF("Configuration saved to NVS");
    return 0;
}

int nvs_storage_load_config(struct ksb_network_config *config)
{
    int ret = nvs_read(&nvs, NVS_CONFIG_KEY, config, sizeof(*config));
    if (ret < 0)
    {
        LOG_WRN("Failed to load config: %d", ret);
        return ret;
    }

    // Validate configuration
    if (!config->is_configured || strlen(config->network_name) == 0)
    {
        LOG_WRN("Invalid configuration in NVS");
        return -EINVAL;
    }

    LOG_INF("Configuration loaded from NVS: %s", config->network_name);
    return 0;
}

int nvs_storage_clear_config(void)
{
    int ret = nvs_delete(&nvs, NVS_CONFIG_KEY);
    if (ret < 0)
    {
        LOG_ERR("Failed to clear config: %d", ret);
        return ret;
    }

    LOG_INF("Configuration cleared from NVS");
    return 0;
}