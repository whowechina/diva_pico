/*
 * Extract gesture information from raw electrode touch bitmask.
 *
 * WHowe <github.com/whowechina>
 */

#ifndef GESTURE_H
#define GESTURE_H

#include <stdbool.h>
#include <stdint.h>

void gesture_init(uint16_t zone_num);
void gesture_set_debug_cluster(bool enable);
void gesture_process(uint32_t mask, uint8_t *axis_a, uint8_t *axis_b);

#endif
