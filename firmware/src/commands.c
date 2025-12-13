#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>

#include "pico/stdio.h"
#include "pico/stdlib.h"

#include "config.h"
#include "slider.h"
#include "hebtn.h"
#include "save.h"
#include "cli.h"

#include "usb_descriptors.h"

#define SENSE_LIMIT_MAX 9
#define SENSE_LIMIT_MIN -9

static void disp_light()
{
    printf("[Light]\n");
    printf("  Slider Level: %d\n", diva_cfg->light.level.slider);
    printf("  Button Level: %d\n", diva_cfg->light.level.button);
}

static void disp_sense()
{
    printf("[Slider]\n");
    printf("  Zone number: %d\n", slider_zone_num());
    printf("  Filter: %u, %u, %u\n", diva_cfg->sense.filter >> 6,
                                    (diva_cfg->sense.filter >> 4) & 0x03,
                                    diva_cfg->sense.filter & 0x07);
    printf("  Sensitivity (global: %+d):\n", diva_cfg->sense.global);
    printf("    | 1| 2| 3| 4| 5| 6| 7| 8| 9|10|11|12|13|14|15|16|\n");
    printf("  ---------------------------------------------------");
    for (int i = 0; i < slider_zone_num(); i++) {
        if (i % 16 == 0) {
            printf("\n    |");
        }
        printf("%+2d|", diva_cfg->sense.keys[i]);
    }
    printf("\n");
    printf("  Debounce (touch, release): %d, %d\n",
           diva_cfg->sense.debounce_touch, diva_cfg->sense.debounce_release);
}

static void disp_hall()
{
    printf("[Hall Button]\n");
    for (int i = 0; i < hebtn_keynum(); i++) {
        if (!hebtn_present(i)) {
            printf("  Key %d: Not Present.\n", i + 1);
            continue;
        }
        printf("  Key %d: %4d->%4d, On: %2d, Off: %2d.\n",
               i + 1, diva_cfg->hall.cali_up[i], diva_cfg->hall.cali_down[i],
                diva_cfg->hall.trig_on[i] + 1, diva_cfg->hall.trig_off[i] + 1);
    }
}

static void disp_hid()
{
    const char *joy_map[] = {"Switch", "Steam", "Arcade"};
    printf("[HID]\n");
    printf("  Joy: %s, NKRO: %s.\n", diva_cfg->hid.joy ? "On" : "Off",
                                     diva_cfg->hid.nkro ? "On" : "Off");
    printf("  Keymap: %s\n", joy_map[diva_cfg->hid.joy_map % 3]);
}

void handle_display(int argc, char *argv[])
{
    const char *usage = "Usage: display [light|sense|hall|hid]\n";
    if (argc > 1) {
        printf(usage);
        return;
    }

    if (argc == 0) {
        disp_light();
        disp_sense();
        disp_hall();
        disp_hid();
        return;
    }

    const char *choices[] = {"light", "sense", "hall", "hid"};
    switch (cli_match_prefix(choices, count_of(choices), argv[0])) {
        case 0:
            disp_light();
            break;
        case 1:
            disp_sense();
            break;
        case 2:
            disp_hall();
            break;
        case 3:
            disp_hid();
            break;
        default:
            printf(usage);
            break;
    }
}

static int fps[2];
void fps_count(int core)
{
    static uint32_t last[2] = {0};
    static int counter[2] = {0};

    counter[core]++;

    uint32_t now = time_us_32();
    if (now - last[core] < 1000000) {
        return;
    }
    last[core] = now;
    fps[core] = counter[core];
    counter[core] = 0;
}

static void handle_level(int argc, char *argv[])
{
    const char *usage = "Usage: level <slider|button> <0..255>\n";
    if (argc != 2) {
        printf(usage);
        return;
    }

    const char *target[] = { "slider", "button" };
    int match = cli_match_prefix(target, 2, argv[0]);
    if (match < 0) {
        printf(usage);
        return;
    }

    int level = cli_extract_non_neg_int(argv[1], 0);
    if ((level < 0) || (level > 255)) {
        printf(usage);
        return;
    }

    if (match == 0) {
        diva_cfg->light.level.slider = level;
    } else if (match == 1) {
        diva_cfg->light.level.button = level;
    }

    config_changed();
    disp_light();
}

static void handle_stat(int argc, char *argv[])
{
    if (argc == 0) {
        for (int col = 0; col < 4; col++) {
            printf(" %2dA |", col * 4 + 1);
            for (int i = 0; i < 4; i++) {
                printf("%6u|", slider_count(col * 8 + i * 2));
            }
            printf("\n   B |");
            for (int i = 0; i < 4; i++) {
                printf("%6u|", slider_count(col * 8 + i * 2 + 1));
            }
            printf("\n");
        }
    } else if ((argc == 1) &&
               (strncasecmp(argv[0], "reset", strlen(argv[0])) == 0)) {
        slider_reset_stat();
    } else {
        printf("Usage: stat [reset]\n");
    }
}

static void handle_hid(int argc, char *argv[])
{
    const char *usage = "Usage: hid <joy|nkro|both>\n";
    if (argc != 1) {
        printf(usage);
        return;
    }

    const char *choices[] = {"joy", "nkro", "both"};
    int match = cli_match_prefix(choices, 3, argv[0]);
    if (match < 0) {
        printf(usage);
        return;
    }

    diva_cfg->hid.joy = ((match == 0) || (match == 2)) ? 1 : 0;
    diva_cfg->hid.nkro = ((match == 1) || (match == 2)) ? 1 : 0;
    config_changed();
    disp_hid();
}

static void handle_keymap(int argc, char *argv[])
{
    const char *usage = "Usage: keymap <switch|steam|arcade>\n";
    if (argc != 1) {
        printf(usage);
        return;
    }
    const char *choices[] = {"switch", "steam", "arcade"};
    int match = cli_match_prefix(choices, 3, argv[0]);
    if (match < 0) {
        printf(usage);
        return;
    }

    diva_cfg->hid.joy_map = match;
    config_changed();
    disp_hid();
}

static void handle_filter(int argc, char *argv[])
{
    const char *usage = "Usage: filter <first> <second> [interval]\n"
                        "Adjusts MPR121 noise filtering parameters (see datasheets).\n"
                        "    first:    First Filter Iterations  (FFI) [0..3]\n"
                        "    second:   Second Filter Iterations (SFI) [0..3]\n"
                        "    interval: Electrode Sample Interval (ESI) [0..7]\n";
    if ((argc < 2) || (argc > 3)) {
        printf(usage);
        return;
    }

    int ffi = cli_extract_non_neg_int(argv[0], 0);
    int sfi = cli_extract_non_neg_int(argv[1], 0);
    int intv = diva_cfg->sense.filter & 0x07;
    if (argc == 3) {
        intv = cli_extract_non_neg_int(argv[2], 0);
    }

    if ((ffi < 0) || (ffi > 3) || (sfi < 0) || (sfi > 3) ||
        (intv < 0) || (intv > 7)) {
        printf(usage);
        return;
    }

    diva_cfg->sense.filter = (ffi << 6) | (sfi << 4) | intv;

    slider_update_config();
    config_changed();
    disp_sense();
}

static int8_t *extract_key(const char *param)
{
    int id = cli_extract_non_neg_int(param, 0) - 1;
    if ((id < 0) || (id > slider_zone_num() - 1)) {
        return NULL;
    }

    return &diva_cfg->sense.keys[id];
}

static void sense_do_op(int8_t *target, char op)
{
    if (op == '+') {
        if (*target < SENSE_LIMIT_MAX) {
            (*target)++;
        }
    } else if (op == '-') {
        if (*target > SENSE_LIMIT_MIN) {
            (*target)--;
        }
    } else if (op == '0') {
        *target = 0;
    }
}

static void handle_sense(int argc, char *argv[])
{
    const char *usage = "Usage: sense [key|*] <+|-|0>\n"
                        "Example:\n"
                        "  >sense +\n"
                        "  >sense -\n"
                        "  >sense 1 +\n"
                        "  >sense 13 -\n"
                        "  >sense * 0\n";
    if ((argc < 1) || (argc > 2)) {
        printf(usage);
        return;
    }

    const char *op = argv[argc - 1];
    if ((strlen(op) != 1) || !strchr("+-0", op[0])) {
        printf(usage);
        return;
    }

    if (argc == 1) {
        sense_do_op(&diva_cfg->sense.global, op[0]);
    } else {
        if (strcmp(argv[0], "*") == 0) {
            for (int i = 0; i < slider_zone_num(); i++) {
                sense_do_op(&diva_cfg->sense.keys[i], op[0]);
            }
        } else {
            int8_t *key = extract_key(argv[0]);
            if (!key) {
                printf(usage);
                return;
            }
            sense_do_op(key, op[0]);
        }
    }

    slider_update_config();
    config_changed();
    disp_sense();
}

static void handle_debounce(int argc, char *argv[])
{
    const char *usage = "Usage: debounce <touch> [release]\n"
                        "  touch, release: 0..7\n";
    if ((argc < 1) || (argc > 2)) {
        printf(usage);
        return;
    }

    int touch = diva_cfg->sense.debounce_touch;
    int release = diva_cfg->sense.debounce_release;
    if (argc >= 1) {
        touch = cli_extract_non_neg_int(argv[0], 0);
    }
    if (argc == 2) {
        release = cli_extract_non_neg_int(argv[1], 0);
    }

    if ((touch < 0) || (release < 0) ||
        (touch > 7) || (release > 7)) {
        printf(usage);
        return;
    }

    diva_cfg->sense.debounce_touch = touch;
    diva_cfg->sense.debounce_release = release;

    slider_update_config();
    config_changed();
    disp_sense();
}

static void handle_raw()
{
    printf("Key raw readings:");
    const uint16_t *raw = slider_raw();
    for (int i = 0; i < slider_zone_num(); i++) {
        if (i % 16 == 0) {
            printf("\n|");
        }
        printf("%3d|", raw[i]);
    }
    printf("\nKey touch status:");
    for (int i = 0; i < slider_zone_num(); i++) {
        if (i % 16 == 0) {
            printf("\n|");
        }
        printf("%2d |", slider_touched(i));
    }
    printf("\n");
}

static void handle_calibrate()
{
    hebtn_calibrate();
}

static void handle_trigger(int argc, char *argv[])
{
    const char *usage = "Usage: trigger <all|KEY> <ON> <OFF>\n"
                        "  KEY: 1..%d\n"
                        "   ON: 1..36, distance for actuation.\n"
                        "  OFF: 1..36, distance for reset.\n";
    if (argc != 3) {
        printf(usage, hebtn_keynum());
        return;
    }

    bool all_key = (strncasecmp(argv[0], "all", strlen(argv[0])) == 0);
    int key = cli_extract_non_neg_int(argv[0], 0) - 1;
    int on = cli_extract_non_neg_int(argv[1], 0) - 1;
    int off = cli_extract_non_neg_int(argv[2], 0) - 1;
    
    if ((!all_key && (key < 0)) || (key >= hebtn_keynum()) ||
        (on < 0) || (on > 35) || (off < 0) || (off > 35)) {
        printf(usage, hebtn_keynum());
        return;
    }

    for (int i = 0; i < hebtn_keynum(); i++) {
        if (all_key || (i == key)) {
            diva_cfg->hall.trig_on[i] = on;
            diva_cfg->hall.trig_off[i] = off;
        }
    }
    config_changed();

    disp_hall();
}

static void handle_debug(int argc, char *argv[])
{
    const char *usage = "Usage: debug <sensor>\n";
    if (argc != 1) {
        printf(usage);
        return;
    }
    const char *choices[] = {"sensor"};
    switch (cli_match_prefix(choices, count_of(choices), argv[0])) {
        case 0:
            diva_runtime.debug.sensor ^= true;
            hebtn_debug(diva_runtime.debug.sensor);
            break;
        default:
            printf(usage);
            break;
    }
}

static void handle_save()
{
    save_request(true);
}

static void handle_factory_reset()
{
    config_factory_reset();
    printf("Factory reset done.\n");
}

void commands_init()
{
    cli_register("display", handle_display, "Display all config.");
    cli_register("level", handle_level, "Set LED brightness level.");
    cli_register("stat", handle_stat, "Display or reset statistics.");
    cli_register("hid", handle_hid, "Set HID mode.");
    cli_register("keymap", handle_keymap, "Set keymap to match game versions.");
    cli_register("filter", handle_filter, "Set pre-filter config.");
    cli_register("sense", handle_sense, "Set sensitivity config.");
    cli_register("debounce", handle_debounce, "Set debounce config.");
    cli_register("raw", handle_raw, "Show key raw readings.");
    cli_register("calibrate", handle_calibrate, "Calibrate hall sensors.");
    cli_register("trigger", handle_trigger, "Set trigger distances for keys.");
    cli_register("debug", handle_debug, "Toggle debug options.");
    cli_register("save", handle_save, "Save config to flash.");
    cli_register("factory", handle_factory_reset, "Reset everything to default.");
}
