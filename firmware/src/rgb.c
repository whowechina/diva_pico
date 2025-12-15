/*
 * RGB LED (WS2812) Strip control
 * WHowe <github.com/whowechina>
 * 
 */

#include "rgb.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "bsp/board.h"
#include "hardware/pio.h"
#include "hardware/timer.h"

#include "ws2812.pio.h"

#include "board_defs.h"
#include "config.h"

#define HID_LIGHT_TIMEOUT_MS 1000

static uint32_t main_buf[37]; // 4(Main buttons) + 1(Start button) + 32(Max Slider)
static uint32_t hid_buf[37];
static uint64_t hid_timeout = 0;

static int8_t button_rgb_map[] = RGB_BUTTON_MAP;

#define _MAP_LED(x) _MAKE_MAPPER(x)
#define _MAKE_MAPPER(x) MAP_LED_##x
#define MAP_LED_RGB { c1 = r; c2 = g; c3 = b; }
#define MAP_LED_GRB { c1 = g; c2 = r; c3 = b; }

#define REMAP_BUTTON_RGB _MAP_LED(BUTTON_RGB_ORDER)
#define REMAP_TT_RGB _MAP_LED(TT_RGB_ORDER)

static inline uint32_t _rgb32(uint32_t c1, uint32_t c2, uint32_t c3, bool gamma_fix)
{
    if (gamma_fix) {
        c1 = ((c1 + 1) * (c1 + 1) - 1) >> 8;
        c2 = ((c2 + 1) * (c2 + 1) - 1) >> 8;
        c3 = ((c3 + 1) * (c3 + 1) - 1) >> 8;
    }
    
    return (c1 << 16) | (c2 << 8) | (c3 << 0);    
}

uint32_t rgb32(uint32_t r, uint32_t g, uint32_t b, bool gamma_fix)
{
#if BUTTON_RGB_ORDER == GRB
    return _rgb32(g, r, b, gamma_fix);
#else
    return _rgb32(r, g, b, gamma_fix);
#endif
}

uint32_t rgb32_from_hsv(uint8_t h, uint8_t s, uint8_t v)
{
    uint32_t region, remainder, p, q, t;

    if (s == 0) {
        return v << 16 | v << 8 | v;
    }

    region = h / 43;
    remainder = (h % 43) * 6;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:
            return v << 16 | t << 8 | p;
        case 1:
            return q << 16 | v << 8 | p;
        case 2:
            return p << 16 | v << 8 | t;
        case 3:
            return p << 16 | q << 8 | v;
        case 4:
            return t << 16 | p << 8 | v;
        default:
            return v << 16 | p << 8 | q;
    }
}

static void drive_led()
{
    static uint64_t last = 0;
    uint64_t now = time_us_64();
    if (now - last < 4000) { // no faster than 250Hz
        return;
    }
    last = now;

    bool use_hid = (hid_timeout > 0) && (now < hid_timeout);
    const uint32_t *buf = use_hid ? hid_buf : main_buf;

    for (int i = 0; i < count_of(main_buf); i++) {
        pio_sm_put_blocking(pio0, 0, buf[i] << 8u);
    }
}

void rgb_set_colors(const uint32_t *colors, unsigned index, size_t num)
{
    if (index >= count_of(main_buf)) {
        return;
    }
    if (index + num > count_of(main_buf)) {
        num = count_of(main_buf) - index;
    }
    memcpy(&main_buf[index], colors, num * sizeof(*colors));
}

static inline uint32_t apply_level(uint32_t color, uint8_t level)
{
    unsigned r = (color >> 16) & 0xff;
    unsigned g = (color >> 8) & 0xff;
    unsigned b = color & 0xff;

    r = r * level / 255;
    g = g * level / 255;
    b = b * level / 255;

    return r << 16 | g << 8 | b;
}

void rgb_button_color(unsigned index, uint32_t color)
{
    if (index < count_of(button_rgb_map)) {
        main_buf[button_rgb_map[index]] = apply_level(color, diva_cfg->light.level.button);
    }
}

void rgb_slider_color(unsigned index, uint32_t color)
{
    if (index > 32) {
        return;
    }
    main_buf[5 + index] = apply_level(color, diva_cfg->light.level.slider);
}

static inline void update_hid_timeout()
{
    hid_timeout = time_us_64() + HID_LIGHT_TIMEOUT_MS * 1000;
}

static inline uint32_t led_rgb(const uint8_t *grb, int index)
{
    const uint8_t *src = grb + index * 3;
    return rgb32(src[1], src[0], src[2], false);
}

void rgb_set_hid_slider(unsigned index, unsigned num, const uint8_t *grb)
{
    for (int i = 0; i < num; i++) {
        if (index + i >= 32) {
            return;
        }
        uint32_t *dest = &hid_buf[5 + index + i];
        *dest = apply_level(led_rgb(grb, i), diva_cfg->light.level.slider);
        }

    update_hid_timeout();
}

void rgb_set_hid_button(const uint8_t *scale)
{
    uint32_t button_colors[] = { 
        rgb32(0, 0xff, 0, false),
        rgb32(0xe0, 0x10, 0xe0, false),
        rgb32(0, 0, 0xff, false),
        rgb32(0xff, 0, 0, false),
    };

    for (int i = 0; i < 4; i++) {
        int button_led = button_rgb_map[i];
        uint32_t color = apply_level(button_colors[i], scale[i]);
        hid_buf[button_led] = apply_level(color, diva_cfg->light.level.button);
    }

    hid_buf[4] = main_buf[4]; // HID doesn't have start button light

    update_hid_timeout();
}

void rgb_init()
{
    uint pio0_offset = pio_add_program(pio0, &ws2812_program);

    gpio_set_drive_strength(RGB_PIN, GPIO_DRIVE_STRENGTH_2MA);
    ws2812_program_init(pio0, 0, pio0_offset, RGB_PIN, 800000, false);
}


void rgb_update()
{
    drive_led();
}
