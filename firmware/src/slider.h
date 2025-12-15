/*
 * Diva Pico Silder Keys
 * WHowe <github.com/whowechina>
 */

#ifndef Silder_H
#define Silder_H

#include <stdint.h>
#include <stdbool.h>

void slider_init();
void slider_update();
uint16_t slider_zone_num();
bool slider_touched(unsigned zone);
uint32_t slider_touch_bits();

const uint16_t *slider_raw();
void slider_update_config();
unsigned slider_count(unsigned zone);
void slider_reset_stat();


#endif
