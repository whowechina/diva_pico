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
        struct {
            uint8_t slider;
            uint8_t button;
        } level;
        uint8_t reserved[2];
    } light;
    struct {
        uint8_t filter;
        int8_t global;
        uint8_t debounce_touch;
        uint8_t debounce_release;        
        int8_t keys[32];
    } sense;
    struct {
        uint8_t joy_map : 4;
        uint8_t empty_bits : 4;
        uint8_t reserved[3];
    } hid;
    struct {
        uint16_t cali_up[4];
        uint16_t cali_down[4];
        uint8_t trig_on[4];
        uint8_t trig_off[4];
    } hall;
} diva_cfg_t;

typedef struct {
    bool diva_plus;
    struct {
        bool sensor;
    } debug;
} diva_runtime_t;

extern diva_cfg_t *diva_cfg;
extern diva_runtime_t diva_runtime;

void config_init();
void config_changed(); // Notify the config has changed
void config_factory_reset(); // Reset the config to factory default

#endif
