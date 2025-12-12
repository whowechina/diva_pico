/*
 * Hall Effect Button Reader
 * WHowe <github.com/whowechina>
 */

#ifndef HEBTN_H
#define HEBTN_H

/* void * is for uint16_t arrays with potentially unaligned address */
void hebtn_init(void *cali_up, void *cali_down, uint8_t *trig_on, uint8_t *trig_off);
void hebtn_update();

void hebtn_debug(bool on);

uint8_t hebtn_keynum();

bool hebtn_any_present();
bool hebtn_present(uint8_t chn);

bool hebtn_actuated(uint8_t chn);

uint32_t hebtn_presence_map();
uint32_t hebtn_read();

uint16_t hebtn_range(uint8_t chn);
uint16_t hebtn_travel(uint8_t chn);

uint8_t hebtn_travel_byte(uint8_t chn);
uint8_t hebtn_trigger_byte(uint8_t chn);

uint16_t hebtn_raw(uint8_t chn);

void hebtn_calibrate();

#endif
