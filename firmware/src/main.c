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

#include "savedata.h"
#include "config.h"
#include "cli.h"
#include "commands.h"

#include "slider.h"
#include "rgb.h"
#include "button.h"
#include "hebtn.h"
#include "lzfx.h"
#include "ps4key.h"
#include "gesture.h"

#define LOG_SAVE_DELAY_SEC 600

struct __attribute__((packed)) {
    uint16_t buttons; // 16 buttons; see JoystickButtons_t for bit mapping
    uint8_t  HAT;    // HAT switch; one nibble w/ unused nibble
    uint32_t axis;  // slider touch data
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
    uint8_t buttons_counter;
    uint8_t trigger_l;
    uint8_t trigger_r;
    uint8_t vendor[54];
} hid_ps4, sent_hid_ps4;

void report_usb_hid()
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
        hid_ns.HAT = 0x08;
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

static uint16_t unified_button_read()
{
    return button_read() | hebtn_read();
}

const static int8_t button_maps[4][7] = {
    { 3, 0, 1, 2, 12, 8, 9 },
    { 0, 3, 2, 1, 12, 8, 9 }, // Steam
    { 3, 0, 1, 2, 9, 12, 8 }, // Arcade
    { 3, 0, 1, 2, 12, -1, -1 }, // PS4
};

static uint16_t raw_buttons;
static uint16_t mapped_buttons;

static void proc_buttons()
{
    raw_buttons = unified_button_read();
    const int8_t *map = button_maps[diva_cfg->hid.joy_map % 4];

    mapped_buttons = 0;
    for (int i = 0; i < 7; i++) {
        if (map[i] >= 0) {
            mapped_buttons |= (raw_buttons & (1 << i)) ? (1 << map[i]) : 0;
        }
    }
}

static uint32_t touch_axis_bits()
{
    uint32_t axis = 0;

    int zone_num = slider_zone_num();
    for (int i = 0; i < zone_num; i++) {
        if (slider_touched(zone_num - 1 - i)) {
            uint32_t bits = zone_num > 16 ? (1 << i) : 0x03 << (i * 2);
            axis |= bits;
        }
    }
    return axis;
}

static uint32_t touch_mask_raw(void)
{
    uint32_t mask = 0;
    int zone_num = slider_zone_num();

    for (int i = 0; i < zone_num; i++) {
        if (slider_touched(i)) {
            mask |= (1u << i);
        }
    }

    return mask;
}

static void gen_ns_report()
{
    hid_ns.axis = touch_axis_bits() ^ 0x80808080;
    hid_ns.buttons = mapped_buttons;
}

static void gen_ps4_report()
{
    uint16_t ps4_buttons = mapped_buttons;
    hid_ps4.left_y = 0x80;
    hid_ps4.right_y = 0x80;
    hid_ps4.buttons_0_3 = ps4_buttons;
    hid_ps4.buttons_11_4 = ps4_buttons >> 4;
    hid_ps4.buttons_counter = ps4_buttons >> 12;
    hid_ps4.trigger_l = 0;
    hid_ps4.trigger_r = 0;

    uint8_t side_buttons = (raw_buttons >> 5) & 0x03;
    // HAT: 0-UP, 4-DOWN, 0x0F-CENTER (aligned with GP2040 PS4 mapping)
    hid_ps4.hat = side_buttons == 0x01 ? 0 : (side_buttons == 0x02 ? 4 : 0x0F);

    gesture_process(touch_mask_raw(), &hid_ps4.left_x, &hid_ps4.right_x);
} 

static void gen_hid_report()
{
    proc_buttons();
    if (diva_runtime.hid_ps4) {
        gen_ps4_report();
    } else {
        gen_ns_report();
    }
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

static void light_update()
{
    static uint64_t next_light = 8000;
    uint64_t now = time_us_64();
    if (now >= next_light) {
        run_lights();
        rgb_update();
        next_light = now + 8000;
    }
}

static void core1_init()
{
    flash_safe_execute_core_init();
}

static void core1_loop()
{
    core1_init();
    while (1) {
        ps4key_job_loop();
        sleep_us(1000);
    }
}

static void core0_loop()
{
    uint64_t next_frame = time_us_64();

    while(1) {
        tud_task();

        cli_run();
    
        if (time_us_64() > LOG_SAVE_DELAY_SEC * 1000000ULL) {
            savedata_save_log();
        }

        savedata_loop();
        cli_fps_count(0);

        button_update();
        slider_update();
        hebtn_update();

        light_update();
    
        gen_hid_report();
        report_usb_hid();

        next_frame += 1000;
        sleep_until(next_frame);
    }
}

void tud_mount_cb(void)
{
    savedata_logf("usb mount");
}

void tud_umount_cb(void)
{
    savedata_logf("usb unmount");
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    savedata_logf("usb suspend");
}

void tud_resume_cb(void)
{
    savedata_logf("usb resume");
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
    } else if (buttons == 0x04) {
        diva_cfg->hid.joy_map = 2;
    } else if (buttons == 0x08) {
        diva_cfg->hid.joy_map = 3;
    } else {
        return;
    }

    diva_runtime.hid_ps4 = (diva_cfg->hid.joy_map == 3);
    hid_use_ps4(diva_runtime.hid_ps4);

    config_changed();
}

void init()
{
    sleep_ms(50);
    set_sys_clock_khz(180000, true);

    update_check();

    config_init();
    savedata_init(0xca44cafe);

    slider_init();
    rgb_init();
    rgb_set_half_mode(slider_zone_num() <= 16);

    button_init();
    hebtn_init(diva_cfg->hall.cali_up, diva_cfg->hall.cali_down,
               diva_cfg->hall.trig_on, diva_cfg->hall.trig_off);

    keymap_check();

    diva_runtime.hid_ps4 = (diva_cfg->hid.joy_map == 3);
    hid_use_ps4(diva_runtime.hid_ps4);

    gesture_reset();
    gesture_set_latch(100);

    board_init();
    tusb_init();
    stdio_init_all();

    ps4key_init();

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
    if (diva_runtime.hid_ps4) {
        if (report_type != HID_REPORT_TYPE_FEATURE) {
            uint16_t resp_len = sizeof(hid_ps4);
            if (resp_len > reqlen) {
                resp_len = reqlen;
            }
            memcpy(buffer, &hid_ps4, resp_len);
            return resp_len;
        }
        return ps4key_get_report(report_id, report_type, buffer, reqlen);
    }
    printf("Get from USB %d-%d\n", report_id, report_type);
    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
    if (diva_runtime.hid_ps4) {
        ps4key_set_report(report_id, report_type, buffer, bufsize);
        return;
    }

    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        uint8_t obuf[48];
        memcpy(obuf, buffer, bufsize);
        if (report_id == REPORT_ID_LED_SLIDER_1) {
            rgb_set_hid_slider(0, 16, obuf, true);
        } else if (report_id == REPORT_ID_LED_SLIDER_2) {
            rgb_set_hid_slider(16, 16, obuf, true);
        } else if (report_id == REPORT_ID_LED_BUTTON) {
            rgb_set_hid_button(obuf);
        }
    } else if (report_type == HID_REPORT_TYPE_FEATURE) {
        if (report_id == REPORT_ID_LED_COMPRESSED) {
            uint8_t fbuf[64];
            memcpy(fbuf, buffer, bufsize);
            uint8_t decomp[100];
            unsigned int olen = sizeof(decomp);
            if (lzfx_decompress(fbuf + 1, fbuf[0], decomp, &olen) != 0) {
                return;
            }
            if (olen < 96) {
                return;
            }
            rgb_set_hid_slider(0, 32, decomp, false);
        }
    }
}
