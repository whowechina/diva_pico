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

static uint16_t readout[16];
static uint16_t touch[2];
static unsigned touch_count[16];

void slider_init()
{
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    
    for (int m = 0; m < 2; m++) {
        mpr121_init(MPR121_ADDR + m);
    }
    slider_update_config();
}

void slider_update()
{
    static uint16_t last_touched[3];

    touch[0] = mpr121_touched(MPR121_ADDR);
    touch[1] = mpr121_touched(MPR121_ADDR + 1);

    for (int m = 0; m < 2; m++) {
        uint16_t just_touched = touch[m] & ~last_touched[m];
        last_touched[m] = touch[m];
        for (int i = 0; i < 8; i++) {
            if (just_touched & (1 << i)) {
                touch_count[m * 8 + i]++;
            }
        }
    }
}

const uint16_t *slider_raw()
{
    mpr121_raw(MPR121_ADDR, readout, 8);
    mpr121_raw(MPR121_ADDR + 1, readout + 8, 8);
    return readout;
}

bool slider_touched(unsigned key)
{
    if (key >= 16) {
        return 0;
    }
    return touch[key / 8] & (1 << (key % 8));
}

unsigned slider_count(unsigned key)
{
    if (key >= 16) {
        return 0;
    }
    return touch_count[key];
}

void slider_reset_stat()
{
    memset(touch_count, 0, sizeof(touch_count));
}

void slider_update_config()
{
    for (int m = 0; m < 2; m++) {
        mpr121_debounce(MPR121_ADDR + m, diva_cfg->sense.debounce_touch,
                                         diva_cfg->sense.debounce_release);
        mpr121_sense(MPR121_ADDR + m, diva_cfg->sense.global,
                                      diva_cfg->sense.keys + m * 8, 8);
        mpr121_filter(MPR121_ADDR + m, diva_cfg->sense.filter >> 6,
                                       (diva_cfg->sense.filter >> 4) & 0x03,
                                       diva_cfg->sense.filter & 0x07);
    }
}
