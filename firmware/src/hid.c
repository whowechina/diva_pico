#include "hid.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "tusb.h"
#include "usb_descriptors.h"

#include "gesture.h"
#include "config.h"
#include "ps4key.h"
#include "rgb.h"
#include "lzfx.h"
#include "slider.h"

struct __attribute__((packed)) {
    uint16_t buttons; // 16 buttons; see JoystickButtons_t for bit mapping
    uint8_t  hat;     // HAT switch; one nibble w/ unused nibble
    uint32_t axis;    // slider touch data
    uint8_t  VendorSpec;
} hid_ns, sent_hid_ns;

struct __attribute__((packed)) {
    uint8_t left_x;
    uint8_t left_y;
    uint8_t right_x;
    uint8_t right_y;
    uint8_t hat : 4;
    uint8_t buttons_0_3 : 4;
    uint8_t buttons_11_4;
    uint8_t buttons_12_15 : 4;
    uint8_t counter : 4;
    uint8_t trigger_l;
    uint8_t trigger_r;
    uint8_t vendor[54];
} hid_ps4, sent_hid_ps4;

#define CON_B1    0
#define CON_B2    1
#define CON_B3    2
#define CON_B4    3
#define CON_R1    4
#define CON_R2    5
#define CON_L1    6

#define NS_B_Y     3
#define NS_B_X     0
#define NS_B_A     1
#define NS_B_B     2
#define NS_B_L     4
#define NS_B_R     5
#define NS_B_MINUS 8
#define NS_B_PLUS  9
#define NS_B_HOME  12

#define PS_B_TR      3
#define PS_B_SQ      0
#define PS_B_X       1
#define PS_B_O       2
#define PS_B_L1      4
#define PS_B_R1      5
#define PS_B_PS      12
#define PS_B_OPTIONS 9
#define PS_B_L3      10
#define PS_B_R3      11

#define HAT_UP     (0x40 | 0)
#define HAT_RIGHT  (0x40 | 2)
#define HAT_DOWN   (0x40 | 4)
#define HAT_LEFT   (0x40 | 6)
#define HAT_CENTER (0x40 | 8)
#define HAT_MASK   0x40

#define L1_SHIFT 0x3f

// B1, B2, B3, B4, R1, R2, L1, B1', B2', B3', B4', R1', R2'
// [0..6]: Function for B1-B7
// [7..12]: Shifted function for B1-B7
// Bit 6 set (0x40) represent HAT values at 4 LSB bits
// Only L1 can be L1_SHIFT (0x3f) to indicate shift key
static const int8_t button_maps[4][13] = {
    // Switch
    {
        NS_B_Y, NS_B_X, NS_B_A, NS_B_B, NS_B_L, NS_B_R, L1_SHIFT,
        HAT_LEFT, HAT_UP, HAT_DOWN, HAT_RIGHT, NS_B_HOME, NS_B_PLUS
    },

    // Steam: different B1-B4 mapping
    {
        NS_B_Y, NS_B_X, NS_B_A, NS_B_B, NS_B_L, NS_B_R, L1_SHIFT,
        HAT_LEFT, HAT_UP, HAT_DOWN, HAT_RIGHT, NS_B_HOME, NS_B_PLUS
    },

    // Arcade: all 7 buttons, no shift
    {
        NS_B_Y, NS_B_X, NS_B_A, NS_B_B, NS_B_MINUS, NS_B_PLUS, NS_B_HOME,
        -1, -1, -1, -1, -1, -1
    },

    // PS4
    {
        PS_B_TR, PS_B_SQ, PS_B_X, PS_B_O, PS_B_L1, PS_B_R1, L1_SHIFT,
        HAT_LEFT, HAT_UP, HAT_DOWN, HAT_RIGHT, PS_B_PS, PS_B_OPTIONS,
    },
};

static bool shift_key_activated = false;

static uint16_t raw_buttons;
static uint32_t raw_touch;
static uint16_t mapped_buttons;
static uint8_t mapped_hat;
static uint32_t mapped_touch;

static void map_buttons(void)
{
    const int8_t *map = button_maps[diva_cfg->hid.joy_map % 4];

    mapped_buttons = 0;
    mapped_hat = HAT_CENTER;

    bool shifted = (map[CON_L1] == L1_SHIFT) && (raw_buttons & (1 << CON_L1));
    shift_key_activated = shifted;

    for (int i = 0; i < 7; i++) {
        if (!(raw_buttons & (1 << i))) {
            continue;
        }

        if (shifted && (i == CON_L1)) {
            continue;
        }

        int target = map[shifted ? (i + 7) : i];

        if (target < 0) {
            continue;
        }

        if (target & HAT_MASK) {
            mapped_hat = target & 0x0f;
            continue;
        }

        if (target < 16) {
            mapped_buttons |= (1 << target);
        }
    }
}

static void map_touch()
{
    mapped_touch = 0;
    int zone_num = slider_zone_num();

    for (int i = 0; i < zone_num; i++) {
        int src = zone_num - 1 - i;
        if (raw_touch & (1u << src)) {
            uint32_t bits = zone_num > 16 ? (1u << i) : (3u << (i * 2));
            mapped_touch |= bits;
        }
    }
}

static void gen_ns_report(void)
{
    hid_ns.axis = mapped_touch ^ 0x80808080;
    hid_ns.buttons = mapped_buttons;
    hid_ns.hat = mapped_hat;
}

static void gen_ps4_report(void)
{
    hid_ps4.left_y = 0x80;
    hid_ps4.right_y = 0x80;
    hid_ps4.counter = 0;
    hid_ps4.trigger_l = 0;
    hid_ps4.trigger_r = 0;

    hid_ps4.buttons_0_3 = mapped_buttons;
    hid_ps4.buttons_11_4 = mapped_buttons >> 4;
    hid_ps4.buttons_12_15 = (mapped_buttons >> 12) & 0x0F;
    hid_ps4.hat = mapped_hat;

    gesture_process(raw_touch, &hid_ps4.left_x, &hid_ps4.right_x);
}

static void report_usb(void)
{
    static uint64_t last_report_joy = 0;

    if (!tud_hid_ready()) {
        return;
    }

    uint64_t now = time_us_64();

    if (diva_runtime.hid_ps4) {
        if ((memcmp(&hid_ps4, &sent_hid_ps4, sizeof(hid_ps4)) != 0) ||
            (now - last_report_joy >= 2000)) {
            if (tud_hid_report(REPORT_ID_JOYSTICK, &hid_ps4, sizeof(hid_ps4))) {
                sent_hid_ps4 = hid_ps4;
            }
            last_report_joy = now;
        }
    } else {
        hid_ns.VendorSpec = 0;
        if ((memcmp(&hid_ns, &sent_hid_ns, sizeof(hid_ns)) != 0) ||
            (now - last_report_joy >= 2000)) {
            if (tud_hid_report(0, &hid_ns, sizeof(hid_ns))) {
                sent_hid_ns = hid_ns;
            }
            last_report_joy = now;
        }
    }
}

void hid_update(uint16_t buttons, uint32_t touch)
{
    raw_buttons = buttons;
    raw_touch = touch;

    map_buttons();
    map_touch();

    if (diva_runtime.hid_ps4) {
        gen_ps4_report();
    } else {
        gen_ns_report();
    }

    report_usb();
}

bool hid_shift_activated(void)
{
    return shift_key_activated;
}

void hid_apply_mode(void)
{
    diva_runtime.hid_ps4 = (diva_cfg->hid.joy_map == 3);
    hid_use_ps4(diva_runtime.hid_ps4);
}
