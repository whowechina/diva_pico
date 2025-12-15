/*
 * Controller Main
 * WHowe <github.com/whowechina>
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "bsp/board.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "tusb.h"
#include "usb_descriptors.h"

#include "board_defs.h"

#include "save.h"
#include "config.h"
#include "cli.h"
#include "commands.h"

#include "slider.h"
#include "rgb.h"
#include "button.h"
#include "hebtn.h"
#include "lzfx.h"

struct __attribute__((packed)) {
    uint16_t buttons; // 16 buttons; see JoystickButtons_t for bit mapping
    uint8_t  HAT;    // HAT switch; one nibble w/ unused nibble
    uint32_t axis;  // slider touch data
    uint8_t  VendorSpec;
} hid_joy, sent_hid_joy;

void report_usb_hid()
{
    static uint64_t last_report_joy = 0;

    if (!tud_hid_ready()) {
        return;
    }

    uint64_t now = time_us_64();

    hid_joy.HAT = 0x08;
    hid_joy.VendorSpec = 0;
    if ((memcmp(&hid_joy, &sent_hid_joy, sizeof(hid_joy)) != 0) ||
        (now - last_report_joy >= 2000)) {
        if (tud_hid_report(0, &hid_joy, sizeof(hid_joy))) {
            sent_hid_joy = hid_joy;
        }
        last_report_joy = now;
    }
}

static uint16_t unified_button_read()
{
    return button_read() | hebtn_read();
}

const static uint8_t maps[3][7] = {
    { 3, 0, 1, 2, 12, 8, 9 },
    { 0, 3, 2, 1, 12, 8, 9 }, // Steam
    { 3, 0, 1, 2, 9, 12, 8 }, // Arcade
};

static void map_buttons()
{
    uint16_t button = unified_button_read();
    const uint8_t *map = maps[diva_cfg->hid.joy_map % 3];

    hid_joy.buttons = 0;
    for (int i = 0; i < 7; i++) {
        hid_joy.buttons |= (button & (1 << i)) ? (1 << map[i]) : 0;
    }
}

static void gen_joy_report()
{
    hid_joy.axis = 0;

    int zone_num = slider_zone_num();
    for (int i = 0; i < zone_num; i++) {
        if (slider_touched(zone_num - 1 - i)) {
            uint32_t bits = zone_num > 16 ? (1 << i) : 0x03 << (i * 2);
            hid_joy.axis |= bits;
        }
    }
    hid_joy.axis ^= 0x80808080;

    map_buttons();

}

static void run_lights()
{
    uint32_t button_colors[] = { 
        rgb32(0, 0xff, 0, false),
        rgb32(0xe0, 0x10, 0xe0, false),
        rgb32(0, 0, 0xff, false),
        rgb32(0xff, 0, 0, false),
        rgb32(0xf0, 0x50, 0x00, false)
    };

    uint16_t buttons = unified_button_read();
    for (int i = 0; i < 5; i++) {
        bool pressed = buttons & (1 << i);
        uint32_t color = pressed ? button_colors[i] : 0x505050;
        rgb_button_color(i, color);
    }

    uint64_t now = time_us_64();

    static int rainbow_level = 0;

    int zone_num = slider_zone_num();
    uint32_t phase = ((now >> 10) / zone_num) & 0xff;
    uint32_t touch_bits = slider_touch_bits();

    if ((touch_bits) && (rainbow_level > 80)) {
        rainbow_level -= (rainbow_level > 168) ? 2 : 1;
    }
    if ((!touch_bits) && (rainbow_level < 256)) {
        rainbow_level += (rainbow_level < 168) ? 2 : 1;
    }

    for (int i = 0; i < zone_num; i++) {
        uint32_t color = rgb32_from_hsv(i * 256 / zone_num + phase, 255, rainbow_level / 2);
        rgb_slider_color(i, touch_bits & (1 << i) ? 0xffffff: color);
    }
}

static mutex_t core1_io_lock;
static void core1_loop()
{
    while (1) {
        if (mutex_try_enter(&core1_io_lock, NULL)) {
            run_lights();
            rgb_update();
            mutex_exit(&core1_io_lock);
        }
        cli_fps_count(1);
        sleep_us(700);
    }
}

static void core0_loop()
{
    uint64_t next_frame = time_us_64();

    while(1) {
        tud_task();

        cli_run();
    
        save_loop();
        cli_fps_count(0);

        button_update();
        slider_update();
        hebtn_update();

        gen_joy_report();
        report_usb_hid();

        next_frame += 1000;
        sleep_until(next_frame);
    }
}

/* if certain key pressed when booting, enter update mode */
static void update_check()
{
    const uint8_t pins[] = BUTTON_DEF;
    int button_num = count_of(pins);
    bool all_pressed = true;
    for (int i = button_num - 2; i < button_num; i++) {
        uint8_t gpio = pins[i];
        gpio_init(gpio);
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_pull_up(gpio);
        sleep_ms(1);
        if (gpio_get(gpio)) {
            all_pressed = false;
            break;
        }
    }

    if (all_pressed) {
        sleep_ms(100);
        reset_usb_boot(0, 2);
        return;
    }
}

static void keymap_check()
{
    button_update();
    hebtn_update();

    uint16_t buttons = unified_button_read();
    if (buttons == 0x01) {
        diva_cfg->hid.joy_map = 0;
    } else if (buttons == 0x02) {
        diva_cfg->hid.joy_map = 1;
    } else if ((buttons == 0x04) || (buttons == 0x08)) {
        diva_cfg->hid.joy_map = 2;
    } else {
        return;
    }
    config_changed();
}

void init()
{
    sleep_ms(50);
    set_sys_clock_khz(150000, true);
    board_init();

    update_check();

    tusb_init();
    stdio_init_all();

    config_init();
    mutex_init(&core1_io_lock);
    save_init(0xca44cafe, &core1_io_lock);

    slider_init();
    rgb_init();
    button_init();
    hebtn_init(diva_cfg->hall.cali_up, diva_cfg->hall.cali_down,
               diva_cfg->hall.trig_on, diva_cfg->hall.trig_off);

    keymap_check();

    cli_init("diva_pico>", "\n   << Diva Pico Controller >>\n"
                            " https://github.com/whowechina\n\n");
    
    commands_init();
}

int main(void)
{
    init();
    multicore_launch_core1(core1_loop);
    core0_loop();
    return 0;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    printf("Get from USB %d-%d\n", report_id, report_type);
    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{

    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        static uint8_t obuf[48];
        memcpy(obuf, buffer, bufsize);
        if (report_id == REPORT_ID_LED_SLIDER_1) {
            rgb_set_hid_slider(0, 16, obuf);
        } else if (report_id == REPORT_ID_LED_SLIDER_2) {
            rgb_set_hid_slider(16, 16, obuf);
        } else if (report_id == REPORT_ID_LED_BUTTON) {
            rgb_set_hid_button(obuf);
        }
    } else if (report_type == HID_REPORT_TYPE_FEATURE) {
        if (report_id == REPORT_ID_LED_COMPRESSED) {
            static uint8_t fbuf[64];
            memcpy(fbuf, buffer, bufsize);
            static uint8_t decomp[100];
            unsigned int olen = sizeof(decomp);
            if (lzfx_decompress(fbuf + 1, fbuf[0], decomp, &olen) != 0) {
                return;
            }
            if (olen < 96) {
                return;
            }
            rgb_set_hid_slider(0, 32, decomp);
        }
    }
}
