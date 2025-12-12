/*
 * Diva Pico Silder Keys
 * WHowe <github.com/whowechina>
 * 
 * MPR121 CapSense based Keys
 */

#include "slider.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "bsp/board.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "board_defs.h"

#include "config.h"
#include "mpr121.h"

#define MPR121_ADDR 0x5A

static uint32_t touch_bits;
static unsigned touch_count[32];

static bool sensor_ok[3];

static uint16_t zone_num = 16;

void slider_init()
{
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    
    for (int m = 0; m < 3; m++) {
        sensor_ok[m] = mpr121_init(MPR121_ADDR + m);
    }
    slider_update_config();

    zone_num = sensor_ok[2] ? 32 : 16;
}

uint16_t slider_zone_num()
{
    return zone_num;
}

void slider_update()
{
    static uint32_t last_touch_bits;
    static uint16_t touch[3];

    for (int m = 0; m < 3; m++) {
        if (!sensor_ok[m]) {
            continue;
        }
        touch[m] = mpr121_touched(MPR121_ADDR + m);
    }

    if (zone_num == 32) {
        touch_bits = (touch[0] & 0x7ff) | ((touch[1] & 0x7ff) << 11) | ((touch[2] & 0x3ff) << 22);
    } else {
        touch_bits = (touch[0] & 0xff) | ((touch[1] & 0xff) << 8);
    }

    uint32_t just_touched = touch_bits & ~last_touch_bits;
    last_touch_bits = touch_bits;

    for (int i = 0; i < zone_num; i++) {
        if (just_touched & (1 << i)) {
            touch_count[i]++;
        }
    }
}

const uint16_t *slider_raw()
{
    static uint16_t raw[32];

    if (zone_num == 32) {
        mpr121_raw(MPR121_ADDR, raw, 11);
        mpr121_raw(MPR121_ADDR + 1, raw + 11, 11);
        mpr121_raw(MPR121_ADDR + 2, raw + 22, 10);
    } else {
        mpr121_raw(MPR121_ADDR, raw, 8);
        mpr121_raw(MPR121_ADDR + 1, raw + 8, 8);
    }

    return raw;
}

bool slider_touched(unsigned zone)
{
    if (zone >= zone_num) {
        return 0;
    }
    return touch_bits & (1 << zone);
}

unsigned slider_count(unsigned zone)
{
    if (zone >= zone_num) {
        return 0;
    }
    return touch_count[zone];
}

void slider_reset_stat()
{
    memset(touch_count, 0, sizeof(touch_count));
}

void slider_update_config()
{
    int zone_per_sensor = (zone_num == 32) ? 11 : 8;

    for (int m = 0; m < 3; m++) {
        if (!sensor_ok[m]) {
            continue;
        }
        mpr121_debounce(MPR121_ADDR + m, diva_cfg->sense.debounce_touch,
                                         diva_cfg->sense.debounce_release);
        mpr121_sense(MPR121_ADDR + m, diva_cfg->sense.global,
                                      diva_cfg->sense.keys + m * zone_per_sensor,
                                      zone_per_sensor);
        mpr121_filter(MPR121_ADDR + m, diva_cfg->sense.filter >> 6,
                                       (diva_cfg->sense.filter >> 4) & 0x03,
                                       diva_cfg->sense.filter & 0x07);
    }
}
