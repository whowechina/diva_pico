/*
 * Hall Effect Button Reader
 * WHowe <github.com/whowechina>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "hebtn.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "board_defs.h"
#include "config.h"

#ifndef HALL_KEY_MUX_EN
#define HALL_KEY_MUX_EN -1
#endif

#ifndef HALL_KEY_ADC_CHN
#define HALL_KEY_ADC_CHN 0
#endif

static bool sensor_debug = false;

static const uint8_t MUX_ADDR[] = HALL_KEY_MUX_ADDRS;
static const uint8_t MUX_KEY_MAP[] = HALL_KEY_MUX_MAP;
static const uint8_t MUX_EN = HALL_KEY_MUX_EN;
static const uint8_t ADC_CHN = HALL_KEY_ADC_CHN;

#define KEY_NUM count_of(MUX_KEY_MAP)
#define ADDR_NUM count_of(MUX_ADDR)

static struct {
    void *cali_up;
    void *cali_down;
    uint8_t *trig_on;
    uint8_t *trig_off;
} setup;

static_assert(KEY_NUM <= (2 << ADDR_NUM), "KEY_NUM never exceeds 2^ADDR_NUM");

static bool hebtn_presence[KEY_NUM];
static bool hebtn_any_presence = false;
static uint16_t reading[KEY_NUM];
static bool key_actuated[KEY_NUM];

static inline void pack16(void *dest, int index, uint16_t value)
{
    union {
        uint8_t u8[2];
        uint16_t value;
    } tmp;

    tmp.value = value;
    uint8_t *d = dest + index * 2;
    d[0] = tmp.u8[0];
    d[1] = tmp.u8[1];
}

static inline uint16_t unpack16(void *src, int index)
{
    union {
        uint8_t u8[2];
        uint16_t value;
    } tmp;

    uint8_t *s = src + index * 2;
    tmp.u8[0] = s[0];
    tmp.u8[1] = s[1];
    return tmp.value;
}

static inline void select_channel(int key)
{
    uint8_t channel = MUX_KEY_MAP[key];
    for (int i = 0; i < ADDR_NUM; i++) {
        gpio_put(MUX_ADDR[i], (channel >> i) & 1);
    }
}

static void read_sensor(int chn, int avg)
{
    uint32_t sum = 0;
    for (int i = 0; i < avg; i++) {
        sum += adc_read();
    }
    reading[chn] = sum / avg;

    if (sensor_debug) {
        if (chn == 0) {
            printf("\n");
        }
        printf(" %d:%4d,", chn, reading[chn]);
    }
}

static void read_all_sensors()
{
    for (int i = 0; i < KEY_NUM; i++) {
        select_channel(i);
        sleep_us(5);
        read_sensor(i, 20);
    }   
}

static void hebtn_discovery()
{
    read_all_sensors();
    hebtn_any_presence = false;
    for (int i = 0; i < KEY_NUM; i++) {
        hebtn_presence[i] = (reading[i] > 200);
        hebtn_any_presence |= hebtn_presence[i];
    }
}

void hebtn_init(void *cali_up, void *cali_down, uint8_t *trig_on, uint8_t *trig_off)
{
    setup.cali_up = cali_up;
    setup.cali_down = cali_down;
    setup.trig_on = trig_on;
    setup.trig_off = trig_off;

    for (int i = 0; i < ADDR_NUM; i++) {
        gpio_init(MUX_ADDR[i]);
        gpio_set_dir(MUX_ADDR[i], GPIO_OUT);
        gpio_put(MUX_ADDR[i], 0);
    }
    if (MUX_EN != -1) {
        gpio_init(MUX_EN);
        gpio_set_dir(MUX_EN, GPIO_OUT);
        gpio_put(MUX_EN, 1);
    }

    adc_init();
    adc_gpio_init(26 + ADC_CHN);
    gpio_pull_down(26 + ADC_CHN);
    adc_select_input(ADC_CHN);

    // pwm mode for lower power ripple
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);
    gpio_put(25, 1);

    hebtn_discovery();
}

uint8_t hebtn_keynum()
{
    return KEY_NUM;
}

bool hebtn_any_present()
{
    return hebtn_any_presence;
}

bool hebtn_present(uint8_t chn)
{
    if (chn >= KEY_NUM) {
        return false;
    }
    return hebtn_presence[chn];
}

uint32_t hebtn_presence_map()
{
    uint32_t bitmap = 0;
    for (int i = 0; i < KEY_NUM; i++) {
        bitmap |= (hebtn_presence[i] << i);
    }
    return bitmap;
}

static void do_triggering()
{
    for (int i = 0; i < KEY_NUM; i++) {
        if (!hebtn_presence[i]) {
            key_actuated[i] = false;
            continue;
        }

        int travel = hebtn_travel(i);

        int on_trig = setup.trig_on[i] % 36 + 1;
        on_trig = on_trig * hebtn_range(i) / 37;

        int off_trig = (35 - setup.trig_off[i] % 36) + 1;
        off_trig = off_trig * on_trig / 38; // 38 for just a bit more dead zone

        key_actuated[i] = key_actuated[i] ? (travel > off_trig)
                                          : (travel >= on_trig);
    }
}

void hebtn_update()
{
    read_all_sensors();
    do_triggering();
}

bool hebtn_actuated(uint8_t chn)
{
    if (chn >= KEY_NUM) {
        return false;
    }
    return key_actuated[chn];
}

uint32_t hebtn_read()
{
    uint32_t bitmap = 0;
    for (int i = 0; i < KEY_NUM; i++) {
        bitmap |= (key_actuated[i] << i);
    }
    return bitmap;
}

uint16_t hebtn_raw(uint8_t chn)
{
    if (chn >= KEY_NUM) {
        return 0;
    }
    return reading[chn];
}

uint16_t hebtn_range(uint8_t chn)
{
    if (chn >= KEY_NUM) {
        return 0;
    }
    return abs(unpack16(setup.cali_down, chn) - unpack16(setup.cali_up, chn));
}

uint16_t hebtn_travel(uint8_t chn)
{
    if (chn >= KEY_NUM) {
        return 0;
    }

    if (!hebtn_presence[chn]) {
        return 0;
    }

    int up = unpack16(setup.cali_up, chn);
    int down = unpack16(setup.cali_down, chn);
    int range = down - up;
    int travel = reading[chn] - up;
    if (range < 0) {
        travel = -travel;
        range = -range;
    }

    if (travel < 8) { // extra start-up dead zone
        travel = 0;
    } else if (travel > range) {
        travel = range;
    }

    return travel;
}

uint8_t hebtn_travel_byte(uint8_t chn)
{
    if (chn >= KEY_NUM) {
        return 0;
    }

    int range = hebtn_range(chn);
    int pos = hebtn_travel(chn);
    return (range != 0) ? pos * 255 / range : 0;
}

uint8_t hebtn_trigger_byte(uint8_t chn)
{
    if (chn >= KEY_NUM) {
        return 0;
    }
    int trig = setup.trig_on[chn] % 36 + 1;

    return trig * 255 / 37;
}

static void read_sensors_avg(uint16_t avg[KEY_NUM])
{
    const int avg_count = 200;
    uint32_t sum[KEY_NUM] = {0};

    for (int i = 0; i < avg_count; i++) {
        for (int j = 0; j < KEY_NUM; j++) {
            select_channel(j);
            sleep_us(5);
            read_sensor(j, 32);
            sum[j] += reading[j];
        }
    }
    for (int i = 0; i < KEY_NUM; i++) {
        avg[i] = sum[i] / avg_count;
    }
}

void hebtn_calibrate()
{
    printf("Calibrating key RELEASED...\n");

    uint16_t up_val[KEY_NUM] = {0};
    read_sensors_avg(up_val);

    printf("Calibrating key PRESSED...\n");
    printf("Please press all keys down, not necessarily simultaneously.\n");

    uint16_t min[KEY_NUM] = {0};
    uint16_t max[KEY_NUM] = {0};
    for (int i = 0; i < KEY_NUM; i++) {
        min[i] = up_val[i];
        max[i] = up_val[i];
    }
    uint64_t stop = time_us_64() + 10000000;
    while (time_us_64() < stop) {
        hebtn_update();
        for (int i = 0; i < KEY_NUM; i++) {
            int val = hebtn_raw(i);
            if (val < min[i]) {
                min[i] = val;
            }
            if (val > max[i]) {
                max[i] = val;
            }
        }
    }

    uint16_t down_val[KEY_NUM] = {0};
    bool success = true;
    for (int i = 0; i < KEY_NUM; i++) {

        int trim = (max[i] - min[i]) / 50; // 2% dead zone at two sides
        max[i] -= trim;
        min[i] += trim;

        bool max_is_down = abs(max[i] - up_val[i]) > abs(min[i] - up_val[i]);
        down_val[i] = max_is_down ? max[i] : min[i];
        up_val[i] = max_is_down ? min[i] : max[i];
        if (abs(down_val[i] - up_val[i]) < 300) {
            printf("Key %d calibration failed. [%4d->%4d].\n",
                   i + 1, up_val[i], down_val[i]);
            success = false;
        }
    }

    printf("Calibration %s.\n", success ? "succeeded" : "failed");

    if (!success) {
        return;
    }

    for (int i = 0; i < KEY_NUM; i++) {
        pack16(setup.cali_up, i, up_val[i]);
        pack16(setup.cali_down, i, down_val[i]);
        printf("Key %d: %4d -> %4d.\n", i + 1, up_val[i], down_val[i]);
    }

    config_changed();
}

void hebtn_debug(bool on)
{
    sensor_debug = on;
}
