#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include "ksb_common.h"

/**
 * Initialize LED control subsystem
 * @return 0 on success, negative error code on failure
 */
int led_control_init(void);

/**
 * Set LED pattern with parameters
 * @param pattern LED pattern to set
 * @param color Base color for the pattern
 * @param brightness Overall brightness (0-255)
 * @param speed Pattern animation speed
 */
void led_control_set_pattern(enum ksb_led_pattern pattern, struct led_rgb color,
                             uint8_t brightness, uint32_t speed);

/**
 * Cycle to next LED pattern (for button control)
 */
void led_control_next_pattern(void);

/**
 * Get current LED pattern
 * @return Current pattern
 */
enum ksb_led_pattern led_control_get_current_pattern(void);

#endif // LED_CONTROL_H