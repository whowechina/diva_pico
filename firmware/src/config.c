/*
 * Controller Config and Runtime Data
 * WHowe <github.com/whowechina>
 * 
 * Config is a global data structure that stores all the configuration
 * Runtime is something to share between files.
 */

#include "config.h"
#include "save.h"

diva_cfg_t *diva_cfg;

static diva_cfg_t default_cfg = {
    .light = {
        .level = { 100, 200 },
    },
    .sense = {
        .filter = 0x10,
        .debounce_touch = 1,
        .debounce_release = 2,
     },
    .hid = {
        .joy_map = 0,
    },
    .hall = {
        .cali_up = { 3600, 3600, 3600, 3600},
        .cali_down = { 2100, 2100, 2100, 2100},
        .trig_on = { 24, 24, 24, 24 },
        .trig_off = { 20, 20, 20, 20 }
    },
};

diva_runtime_t diva_runtime;

static void config_loaded()
{
    if ((diva_cfg->sense.filter & 0x0f) > 3 ||
        ((diva_cfg->sense.filter >> 4) & 0x0f) > 3) {
        diva_cfg->sense.filter = default_cfg.sense.filter;
        config_changed();
    }
    if ((diva_cfg->sense.global > 9) || (diva_cfg->sense.global < -9)) {
        diva_cfg->sense.global = default_cfg.sense.global;
        config_changed();
    }
    for (int i = 0; i < 32; i++) {
        if ((diva_cfg->sense.keys[i] > 9) || (diva_cfg->sense.keys[i] < -9)) {
            diva_cfg->sense.keys[i] = default_cfg.sense.keys[i];
            config_changed();
        }
    }
    if ((diva_cfg->sense.debounce_touch > 7) |
        (diva_cfg->sense.debounce_release > 7)) {
        diva_cfg->sense.debounce_touch = default_cfg.sense.debounce_touch;
        diva_cfg->sense.debounce_release = default_cfg.sense.debounce_release;
        config_changed();
    }
}

void config_changed()
{
    save_request(false);
}

void config_factory_reset()
{
    *diva_cfg = default_cfg;
    save_request(true);
}

void config_init()
{
    diva_cfg = (diva_cfg_t *)save_alloc(sizeof(*diva_cfg), &default_cfg, config_loaded);
}
