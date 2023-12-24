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
    .colors = {
        .key_on_upper = 0x00FF00,
        .key_on_lower = 0xff0000,
        .key_on_both = 0xff0000,
        .key_off = 0x000000,
        .gap = 0x000000,
    },
    .style = {
        .key = 0,
        .gap = 0,
        .tof = 0,
        .level = 127,
    },
    .tof = {
        .offset = 80,
        .pitch = 20,
    },
    .sense = {
        .filter = 0x10,
        .debounce_touch = 1,
        .debounce_release = 2,
     },
    .hid = {
        .joy = 1,
        .nkro = 0,
    },
};

diva_runtime_t *diva_runtime;

static void config_loaded()
{
    if (diva_cfg->style.level > 10) {
        diva_cfg->style.level = default_cfg.style.level;
        config_changed();
    }
    if ((diva_cfg->tof.offset < 40) ||
        (diva_cfg->tof.pitch < 4) || (diva_cfg->tof.pitch > 50)) {
        diva_cfg->tof = default_cfg.tof;
        config_changed();
    }
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
