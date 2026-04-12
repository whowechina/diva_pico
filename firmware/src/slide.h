/*
 * Slider gesture extraction for PS4 HID mapping
 */

#ifndef SLIDE_H
#define SLIDE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SLIDE_GESTURE_NONE = 0,
    SLIDE_GESTURE_SINGLE_LEFT,
    SLIDE_GESTURE_SINGLE_RIGHT,
    SLIDE_GESTURE_DUAL_LEFT,
    SLIDE_GESTURE_DUAL_RIGHT,
    SLIDE_GESTURE_DUAL_CONVERGE,
    SLIDE_GESTURE_DUAL_DIVERGE,
} slide_gesture_t;

typedef struct {
    slide_gesture_t gesture;
    uint8_t left_x;
    uint8_t right_x;
    uint8_t cluster_count;
} slide_result_t;

void slide_reset(void);
void slide_set_latch(uint16_t frames);
void slide_set_debug_cluster(bool enable);
void slide_process(uint32_t mask, slide_result_t *out);

#endif
