/*
 * Controller Config
 * WHowe <github.com/whowechina>
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

typedef struct __attribute__((packed)) {
    struct {
        uint32_t not_used[3];
    } colors;
    struct {
        uint8_t not_used[3];
        uint8_t level;
    } light;
    uint8_t not_used2[2];
    struct {
        uint8_t filter;
        int8_t global;
        uint8_t debounce_touch;
        uint8_t debounce_release;        
        int8_t keys[32];
    } sense;
    struct {
        uint8_t joy : 1;
        uint8_t joy_map : 3;
        uint8_t nkro : 1;
        uint8_t not_used : 3;
    } hid;
} diva_cfg_t;

typedef struct {
    uint16_t fps[2];
} diva_runtime_t;

extern diva_cfg_t *diva_cfg;
extern diva_runtime_t *diva_runtime;

void config_init();
void config_changed(); // Notify the config has changed
void config_factory_reset(); // Reset the config to factory default

#endif
