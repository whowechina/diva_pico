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
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "tusb.h"
#include "usb_descriptors.h"

#include "board_defs.h"

#include "save.h"
#include "config.h"
#include "cli.h"
#include "commands.h"
#include "aime.h"

#include "slider.h"
#include "rgb.h"
#include "button.h"
#include "lzfx.h"

struct __attribute__((packed)) {
    uint16_t buttons; // 16 buttons; see JoystickButtons_t for bit mapping
    uint8_t  HAT;    // HAT switch; one nibble w/ unused nibble
    uint32_t axis;  // slider touch data
    uint8_t  VendorSpec;
} hid_joy;

struct __attribute__((packed)) {
    uint8_t modifier;
    uint8_t keymap[15];
} hid_nkro, sent_hid_nkro;

void report_usb_hid()
{
    if (tud_hid_ready()) {
        hid_joy.HAT = 0x08;
        hid_joy.VendorSpec = 0;
        if (diva_cfg->hid.joy) {
            tud_hid_n_report(0x00, 0, &hid_joy, sizeof(hid_joy));
        }
        if (diva_cfg->hid.nkro &&
            (memcmp(&hid_nkro, &sent_hid_nkro, sizeof(hid_nkro)) != 0)) {
            sent_hid_nkro = hid_nkro;
            tud_hid_n_report(0x02, 0, &sent_hid_nkro, sizeof(sent_hid_nkro));
        }
    }
}

static void gen_joy_report()
{
    hid_joy.axis = 0;
    for (int i = 0; i < 16; i++) {
        if (slider_touched(15 - i)) {
            hid_joy.axis |= 0x03 << (i * 2);
        }
    }
    hid_joy.axis ^= 0x80808080;
    uint16_t button = button_read();
    hid_joy.buttons = (button & 0x0f) |
                     ((button & 0x10) << 8) |
                     ((button & 0x60) << 3);

}

const uint8_t keycode_table[128][2] = { HID_ASCII_TO_KEYCODE };
const uint8_t keymap[38 + 1] = NKRO_KEYMAP; // 32 keys, 6 air keys, 1 terminator
static void gen_nkro_report()
{
    for (int i = 0; i < 32; i++) {
        uint8_t code = keycode_table[keymap[i]][1];
        uint8_t byte = code / 8;
        uint8_t bit = code % 8;
        if (slider_touched(i)) {
            hid_nkro.keymap[byte] |= (1 << bit);
        } else {
            hid_nkro.keymap[byte] &= ~(1 << bit);
        }
    }
    for (int i = 0; i < 6; i++) {
        uint8_t code = keycode_table[keymap[32 + i]][1];
        uint8_t byte = code / 8;
        uint8_t bit = code % 8;
        if (hid_joy.buttons & (1 << i)) {
            hid_nkro.keymap[byte] |= (1 << bit);
        } else {
            hid_nkro.keymap[byte] &= ~(1 << bit);
        }
    }
}

static uint64_t last_hid_time = 0;

static void run_lights()
{
    uint64_t now = time_us_64();
    uint32_t button_colors[] = { 
        rgb32(0x70, 0x08, 0x50, false),
        rgb32(0x80, 0, 0, false),
        rgb32(0, 0, 0x80, false),
        rgb32(0, 0x80, 0, false),
        rgb32(0x10, 0x10, 0x10, false)
    };

    uint16_t buttons = button_read();
    for (int i = 0; i < 5; i++) {
        bool pressed = buttons & (1 << i);
        rgb_button_color(i, pressed ? button_colors[i] : 0x808080);
    }

    if (now - last_hid_time >= 1000000) {
        for (int i = 0; i < 16; i++) {
            uint32_t color = rgb32_from_hsv(i * 16, 255, 255);
            rgb_slider_color(i, slider_touched(i) ? color : 0x101010);
        }
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
        sleep_ms(1);
    }
}

static void core0_loop()
{
    while(1) {
        tud_task();

        cli_run();
        aime_update();
    
        save_loop();
        cli_fps_count(0);

        button_update();
        slider_update();

        gen_joy_report();
        gen_nkro_report();
        report_usb_hid();
    }
}

/* if certain key pressed when booting, enter update mode */
static void update_check()
{
    const uint8_t pins[] = BUTTON_DEF; // keypad 00 and *
    bool all_pressed = true;
    for (int i = 0; i < 2; i++) {
        uint8_t gpio = pins[sizeof(pins) - 2 + i];
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

    aime_init(1);

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


struct __attribute__((packed)) {
    uint16_t buttons;
    uint8_t  HAT;
    uint32_t axis;
} hid_joy_out = {0};

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
    return;

    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        if (report_id == REPORT_ID_LED_SLIDER_16) {
            rgb_set_brg(0, buffer, bufsize / 3);
        } else if (report_id == REPORT_ID_LED_SLIDER_15) {
            rgb_set_brg(16, buffer, bufsize / 3);
        } else if (report_id == REPORT_ID_LED_TOWER_6) {
            rgb_set_brg(31, buffer, bufsize / 3);
        }
        last_hid_time = time_us_64();
        return;
    } 
    
    if (report_type == HID_REPORT_TYPE_FEATURE) {
        if (report_id == REPORT_ID_LED_COMPRESSED) {
            uint8_t buf[(48 + 45 + 6) * 3];
            unsigned int olen = sizeof(buf);
            if (lzfx_decompress(buffer + 1, buffer[0], buf, &olen) == 0) {
                rgb_set_brg(0, buf, olen / 3);
            }

            if (!diva_cfg->hid.joy) {
                diva_cfg->hid.joy = 1;
                config_changed();
            }
        }
        last_hid_time = time_us_64();
        return;
    }
}
