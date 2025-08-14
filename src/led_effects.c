#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>
#include <math.h>
#include "keyasoftbox.h"

LOG_MODULE_REGISTER(led_effects, LOG_LEVEL_INF);

/* External references */
extern const struct device *const strip;
extern struct keyasoftbox_state device_state;

/* Animation state */
static struct animation_state {
    uint32_t frame_counter;
    uint8_t hue_offset;
    uint8_t breath_intensity;
    bool breath_direction;
    uint8_t wave_position;
    uint8_t fire_heat[NUM_PIXELS];
} anim_state = {0};

/* Color utility functions */
void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, struct led_rgb *rgb)
{
    uint8_t region, remainder, p, q, t;

    if (s == 0) {
        rgb->r = v;
        rgb->g = v;
        rgb->b = v;
        return;
    }

    region = h / 43;
    remainder = (h - (region * 43)) * 6;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:
            rgb->r = v; rgb->g = t; rgb->b = p;
            break;
        case 1:
            rgb->r = q; rgb->g = v; rgb->b = p;
            break;
        case 2:
            rgb->r = p; rgb->g = v; rgb->b = t;
            break;
        case 3:
            rgb->r = p; rgb->g = q; rgb->b = v;
            break;
        case 4:
            rgb->r = t; rgb->g = p; rgb->b = v;
            break;
        default:
            rgb->r = v; rgb->g = p; rgb->b = q;
            break;
    }
}

uint8_t scale8(uint8_t value, uint8_t scale)
{
    return ((uint16_t)value * scale) >> 8;
}

/* Animation Effects */

void effect_static_color(void)
{
    /* Static color - no animation needed, just maintain current state */
}

void effect_rainbow_cycle(void)
{
    for (int i = 0; i < NUM_PIXELS; i++) {
        uint8_t hue = anim_state.hue_offset + (i * 255 / NUM_PIXELS);
        struct led_rgb color;
        hsv_to_rgb(hue, 255, device_state.brightness, &color);
        device_state.pixels[i] = color;
    }
    
    anim_state.hue_offset += 2;
}

void effect_breathing(void)
{
    static const struct led_rgb base_color = {255, 255, 255}; // White base
    
    if (anim_state.breath_direction) {
        anim_state.breath_intensity += 3;
        if (anim_state.breath_intensity >= 255) {
            anim_state.breath_direction = false;
        }
    } else {
        anim_state.breath_intensity -= 3;
        if (anim_state.breath_intensity <= 20) {
            anim_state.breath_direction = true;
        }
    }
    
    for (int i = 0; i < NUM_PIXELS; i++) {
        device_state.pixels[i].r = scale8(base_color.r, anim_state.breath_intensity);
        device_state.pixels[i].g = scale8(base_color.g, anim_state.breath_intensity);
        device_state.pixels[i].b = scale8(base_color.b, anim_state.breath_intensity);
    }
}

void effect_color_wipe(void)
{
    static const struct led_rgb colors[] = {
        {255, 0, 0},   // Red
        {0, 255, 0},   // Green
        {0, 0, 255},   // Blue
        {255, 255, 0}, // Yellow
        {255, 0, 255}, // Magenta
        {0, 255, 255}, // Cyan
    };
    
    int color_index = (anim_state.frame_counter / (NUM_PIXELS + 10)) % ARRAY_SIZE(colors);
    int position = anim_state.frame_counter % (NUM_PIXELS + 10);
    
    // Clear all pixels first
    memset(device_state.pixels, 0, sizeof(device_state.pixels));
    
    // Fill pixels up to current position
    for (int i = 0; i < position && i < NUM_PIXELS; i++) {
        device_state.pixels[i] = colors[color_index];
        device_state.pixels[i].r = scale8(device_state.pixels[i].r, device_state.brightness);
        device_state.pixels[i].g = scale8(device_state.pixels[i].g, device_state.brightness);
        device_state.pixels[i].b = scale8(device_state.pixels[i].b, device_state.brightness);
    }
    
    anim_state.frame_counter++;
}

void effect_rainbow_wave(void)
{
    for (int i = 0; i < NUM_PIXELS; i++) {
        uint8_t hue = (anim_state.wave_position + i * 20) % 255;
        struct led_rgb color;
        hsv_to_rgb(hue, 255, device_state.brightness, &color);
        device_state.pixels[i] = color;
    }
    
    anim_state.wave_position += 5;
}

void effect_fire(void)
{
    // Cool down every pixel a little
    for (int i = 0; i < NUM_PIXELS; i++) {
        uint8_t cooldown = sys_rand32_get() % 15;  // Random cooling
        if (cooldown > anim_state.fire_heat[i]) {
            anim_state.fire_heat[i] = 0;
        } else {
            anim_state.fire_heat[i] -= cooldown;
        }
    }
    
    // Heat from each pixel drifts 'up' and diffuses
    for (int i = NUM_PIXELS - 1; i >= 2; i--) {
        anim_state.fire_heat[i] = (anim_state.fire_heat[i - 1] + 
                                   anim_state.fire_heat[i - 2] + 
                                   anim_state.fire_heat[i - 2]) / 3;
    }
    
    // Randomly ignite new 'sparks' at the bottom
    if (sys_rand32_get() % 10 < 6) {  // 60% chance
        int pos = sys_rand32_get() % 3;  // Bottom 3 pixels
        anim_state.fire_heat[pos] = MIN(255, anim_state.fire_heat[pos] + sys_rand32_get() % 95 + 160);
    }
    
    // Convert heat to LED colors
    for (int i = 0; i < NUM_PIXELS; i++) {
        uint8_t heat = anim_state.fire_heat[i];
        struct led_rgb color = {0, 0, 0};
        
        if (heat > 0) {
            // Heat ramp: black -> red -> yellow -> white
            if (heat < 85) {
                color.r = heat * 3;
            } else if (heat < 170) {
                color.r = 255;
                color.g = (heat - 85) * 3;
            } else {
                color.r = 255;
                color.g = 255;
                color.b = (heat - 170) * 3;
            }
        }
        
        device_state.pixels[i].r = scale8(color.r, device_state.brightness);
        device_state.pixels[i].g = scale8(color.g, device_state.brightness);
        device_state.pixels[i].b = scale8(color.b, device_state.brightness);
    }
}

void effect_twinkle(void)
{
    // Fade all pixels slightly
    for (int i = 0; i < NUM_PIXELS; i++) {
        device_state.pixels[i].r = scale8(device_state.pixels[i].r, 250);
        device_state.pixels[i].g = scale8(device_state.pixels[i].g, 250);
        device_state.pixels[i].b = scale8(device_state.pixels[i].b, 250);
    }
    
    // Randomly light up pixels
    if (sys_rand32_get() % 4 == 0) {  // 25% chance each frame
        int pixel = sys_rand32_get() % NUM_PIXELS;
        uint8_t hue = sys_rand32_get() % 255;
        struct led_rgb color;
        hsv_to_rgb(hue, 200, device_state.brightness, &color);
        device_state.pixels[pixel] = color;
    }
}

void effect_chase(void)
{
    static const struct led_rgb chase_color = {255, 100, 0}; // Orange
    int position = anim_state.frame_counter % (NUM_PIXELS * 2);
    
    // Clear all pixels
    memset(device_state.pixels, 0, sizeof(device_state.pixels));
    
    // Set chase pixels (3 pixels wide)
    for (int i = 0; i < 3; i++) {
        int pixel_pos = (position + i) % NUM_PIXELS;
        if (pixel_pos < NUM_PIXELS) {
            device_state.pixels[pixel_pos].r = scale8(chase_color.r, device_state.brightness);
            device_state.pixels[pixel_pos].g = scale8(chase_color.g, device_state.brightness);
            device_state.pixels[pixel_pos].b = scale8(chase_color.b, device_state.brightness);
        }
    }
    
    anim_state.frame_counter++;
}

void effect_pulse_colors(void)
{
    static const struct led_rgb colors[] = {
        {255, 0, 0},   // Red
        {0, 255, 0},   // Green
        {0, 0, 255},   // Blue
    };
    
    int color_index = (anim_state.frame_counter / 60) % ARRAY_SIZE(colors);
    uint8_t pulse = (uint8_t)(128 + 127 * sin(anim_state.frame_counter * 0.1));
    
    for (int i = 0; i < NUM_PIXELS; i++) {
        device_state.pixels[i].r = scale8(scale8(colors[color_index].r, pulse), device_state.brightness);
        device_state.pixels[i].g = scale8(scale8(colors[color_index].g, pulse), device_state.brightness);
        device_state.pixels[i].b = scale8(scale8(colors[color_index].b, pulse), device_state.brightness);
    }
    
    anim_state.frame_counter++;
}

/* Animation dispatcher */
void run_led_effect(uint8_t effect_type)
{
    if (!device_state.power_on) {
        memset(device_state.pixels, 0, sizeof(device_state.pixels));
        return;
    }
    
    switch (effect_type) {
        case EFFECT_STATIC:
            effect_static_color();
            break;
        case EFFECT_RAINBOW_CYCLE:
            effect_rainbow_cycle();
            break;
        case EFFECT_BREATHING:
            effect_breathing();
            break;
        case EFFECT_COLOR_WIPE:
            effect_color_wipe();
            break;
        case EFFECT_RAINBOW_WAVE:
            effect_rainbow_wave();
            break;
        case EFFECT_FIRE:
            effect_fire();
            break;
        case EFFECT_TWINKLE:
            effect_twinkle();
            break;
        case EFFECT_CHASE:
            effect_chase();
            break;
        case EFFECT_PULSE_COLORS:
            effect_pulse_colors();
            break;
        default:
            effect_static_color();
            break;
    }
}

/* Reset animation state */
void reset_animation_state(void)
{
    memset(&anim_state, 0, sizeof(anim_state));
    anim_state.breath_direction = true;
}