#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(ksb_main, LOG_LEVEL_DBG);

int main(void)
{
    while (1)
    {
        LOG_INF("Initial check");
        k_msleep(500);
    }
    return 0;
}