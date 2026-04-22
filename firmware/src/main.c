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
#include "hid.h"

#include "slider.h"
#include "rgb.h"
#include "button.h"
#include "hebtn.h"
#include "lzfx.h"
#include "ps4key.h"
#include "gesture.h"

#define LOG_SAVE_DELAY_SEC 600

static uint16_t unified_button_read()
{
    return button_read() | hebtn_read();
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

static void run_lights()
{
    uint32_t button_colors[] = { 
        rgb32(0, 0xff, 0, false),
        rgb32(0xe0, 0x10, 0xe0, false),
        rgb32(0, 0, 0xff, false),
        rgb32(0xff, 0, 0, false),
        rgb32(0xf0, 0x50, 0x00, false)
    };

    uint64_t now = time_us_64();

    uint16_t buttons = unified_button_read();
    for (int i = 0; i < 5; i++) {
        bool pressed = buttons & (1 << i);
        uint32_t shifted_color = ((now >> 17) % 3) ? 0x7f7f00 : 0x1f1f00;
        uint32_t off_color = hid_shift_activated() ? shifted_color : 0x505050;
        uint32_t color = pressed ? button_colors[i] : off_color;
        rgb_button_color(i, color);
    }


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
    run_lights();
    rgb_update();
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

        hid_update(unified_button_read(), touch_mask_raw());

        light_update();
   
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
    button_update();
    uint16_t buttons = button_read();
    if (((buttons >> 4) & 0x03) == 0x03) {
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
    }

    hid_apply_mode();
    config_changed();
}

void init()
{
    sleep_ms(50);
    set_sys_clock_khz(180000, true);

    button_init();
    update_check();

    config_init();
    savedata_init(0xca44cafe);

    slider_init();
    rgb_init();
    rgb_set_half_mode(slider_zone_num() <= 16);

    hebtn_init(diva_cfg->hall.cali_up, diva_cfg->hall.cali_down,
               diva_cfg->hall.trig_on, diva_cfg->hall.trig_off);

    keymap_check();

    gesture_reset();

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
