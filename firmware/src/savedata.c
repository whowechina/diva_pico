/*
 * Controller Config Save and Load
 * WHowe <github.com/whowechina>
 * 
 * Config is stored in last sector of flash
 */

#include "savedata.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <memory.h>
#include <stdarg.h>

#include "pico/bootrom.h"
#include "pico/stdio.h"
#include "pico/mutex.h"

#include "hardware/flash.h"
#include "pico/multicore.h"

static struct {
    size_t size;
    size_t offset;
    void (*after_load)();
} modules[8] = {0};
static int module_num = 0;

static uint32_t my_magic = 0xcafecafe;

#define SAVE_TIMEOUT_US 5000000
#define LOG_SECTOR_NUM 2

#define SAVE_SECTOR_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define GLOBAL_SECTOR_NUM 1
#define GLOBAL_SECTOR_SIZE (GLOBAL_SECTOR_NUM * FLASH_SECTOR_SIZE)
#define GLOBAL_SECTOR_OFFSET (SAVE_SECTOR_OFFSET - GLOBAL_SECTOR_SIZE)
#define LOG_SECTOR_SIZE (LOG_SECTOR_NUM * FLASH_SECTOR_SIZE)
#define LOG_SECTOR_OFFSET (GLOBAL_SECTOR_OFFSET - LOG_SECTOR_SIZE)

static uint8_t global_write_buffer[GLOBAL_SECTOR_SIZE];
static char log_ram_buffer[LOG_SECTOR_SIZE];
static size_t log_ram_length = 0;

static bool log_ram_truncated = false;

static bool log_persisted = false;
static bool request_log_persist = false;

static mutex_t log_mutex;

typedef struct __attribute ((packed)) {
    uint32_t magic;
    uint8_t data[FLASH_PAGE_SIZE - 4];
} page_t;

static page_t old_data = {0};
static page_t new_data = {0};
static page_t default_data = {0};
static int data_page = -1;

static bool requesting_save = false;
static uint64_t requesting_time = 0;


static void do_write(void *param)
{
        if (data_page == 0) {
            flash_range_erase(SAVE_SECTOR_OFFSET, FLASH_SECTOR_SIZE);
        }
        flash_range_program(SAVE_SECTOR_OFFSET + data_page * FLASH_PAGE_SIZE,
                            (uint8_t *)&old_data, FLASH_PAGE_SIZE);
}

static void do_write_log(void *param)
{
    uintptr_t *p = (uintptr_t *)param;
    const uint8_t *data = (const uint8_t *)p[0];
    size_t size = (size_t)p[1];

    flash_range_erase(LOG_SECTOR_OFFSET, LOG_SECTOR_SIZE);
    flash_range_program(LOG_SECTOR_OFFSET, data, size);
}

static void save_program()
{
    old_data = new_data;
    data_page = (data_page + 1) % (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE);
    printf("\nProgram Save %d %8lx ", data_page, old_data.magic);
    if (flash_safe_execute(do_write, NULL, 1000) != PICO_OK) {
        printf("Failed!\n");
    } else {
        printf("Done.\n");
    }
}

static void load_default()
{
    printf("Load Default\n");
    new_data = default_data;
    new_data.magic = my_magic;
}

static const page_t *get_page(int id)
{
    int addr = XIP_BASE + SAVE_SECTOR_OFFSET;
    return (page_t *)(addr + FLASH_PAGE_SIZE * id);
}

static void save_load()
{
    for (int i = 0; i < FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE; i++) {
        if (get_page(i)->magic != my_magic) {
            break;
        }
        data_page = i;
    }

    if (data_page < 0) {
        load_default();
        savedata_request(false);
        return;
    }

    old_data = *get_page(data_page);
    new_data = old_data;
    printf("Page Loaded %d %8lx\n", data_page, new_data.magic);
}

static void save_loaded()
{
    for (int i = 0; i < module_num; i++) {
        modules[i].after_load();
    }
}

static void log_init()
{
    mutex_init(&log_mutex);
    memset(log_ram_buffer, 0xff, sizeof(log_ram_buffer));
    log_ram_buffer[0] = '\0';
}

void savedata_init(uint32_t magic)
{
    my_magic = magic;
    save_load();
    savedata_loop();
    save_loaded();

    log_init();
}

void savedata_save_log()
{
    request_log_persist = true;
}

void *savedata_alloc(size_t size, void *def, void (*after_load)())
{
    size_t offset = 0;
    if (module_num > 0) {
        offset = modules[module_num - 1].offset + modules[module_num - 1].size;
    }
    if (offset + size > sizeof(default_data.data)) {
        return NULL;
    }
    modules[module_num].size = size;
    modules[module_num].offset = offset;
    modules[module_num].after_load = after_load;
    module_num++;
    memcpy(default_data.data + offset, def, size); // backup the default
    return new_data.data + offset;
}

void savedata_request(bool immediately)
{
    if (!requesting_save) {
        printf("Save requested.\n");
        requesting_save = true;
        new_data.magic = my_magic;
        requesting_time = time_us_64();
    }
    if (immediately) {
        requesting_time = 0;
        savedata_loop();
    }
}

void *savedata_get_global()
{
    return (void *)(XIP_BASE + GLOBAL_SECTOR_OFFSET);
}

static void do_write_global(void *param)
{
    uintptr_t *p = (uintptr_t *)param;
    const uint8_t *data = (const uint8_t *)p[0];
    size_t size = (size_t)p[1];

    flash_range_erase(GLOBAL_SECTOR_OFFSET, GLOBAL_SECTOR_NUM * FLASH_SECTOR_SIZE);
    flash_range_program(GLOBAL_SECTOR_OFFSET, data, size);
}

void savedata_write_global(const void *data, size_t size)
{
    static uintptr_t param[2];
    if (data == NULL) {
        return;
    }

    if (size > GLOBAL_SECTOR_SIZE) {
        size = GLOBAL_SECTOR_SIZE;
    }

    size_t padded = size;
    if ((padded % FLASH_PAGE_SIZE) != 0) {
        padded += FLASH_PAGE_SIZE - (padded % FLASH_PAGE_SIZE);
    }
    if (padded == 0) {
        padded = FLASH_PAGE_SIZE;
    }

    memset(global_write_buffer, 0xff, sizeof(global_write_buffer));
    memcpy(global_write_buffer, data, size);

    param[0] = (uintptr_t)global_write_buffer;
    param[1] = padded;

    printf("Program Global %08x ", GLOBAL_SECTOR_OFFSET);
    if (flash_safe_execute(do_write_global, param, 1000) != PICO_OK) {
        printf("Failed!\n");
    } else {
        printf("Done.\n");
    }
}

void savedata_clear_global()
{
    memset(global_write_buffer, 0xff, sizeof(global_write_buffer));
    savedata_write_global(global_write_buffer, FLASH_PAGE_SIZE);
}

size_t savedata_global_size()
{
    return GLOBAL_SECTOR_SIZE;
}

void savedata_logf(const char *fmt, ...)
{
    if (fmt == NULL) {
        return;
    }

    mutex_enter_blocking(&log_mutex);

    if (log_persisted || log_ram_truncated || (log_ram_length >= sizeof(log_ram_buffer) - 1)) {
        mutex_exit(&log_mutex);
        return;
    }

    uint32_t time_ms = time_us_32() / 1000;
    int written = snprintf(log_ram_buffer + log_ram_length,
                           sizeof(log_ram_buffer) - log_ram_length,
                           "%lu: ",
                           (unsigned long)time_ms);
    if ((written < 0) || ((size_t)written >= (sizeof(log_ram_buffer) - log_ram_length))) {
        log_ram_length = sizeof(log_ram_buffer) - 1;
        log_ram_buffer[log_ram_length] = '\0';
        log_ram_truncated = true;
        mutex_exit(&log_mutex);
        return;
    }
    log_ram_length += (size_t)written;

    va_list args;
    va_start(args, fmt);
    written = vsnprintf(log_ram_buffer + log_ram_length,
                        sizeof(log_ram_buffer) - log_ram_length,
                        fmt,
                        args);
    va_end(args);
    if (written < 0) {
        mutex_exit(&log_mutex);
        return;
    }

    if ((size_t)written >= (sizeof(log_ram_buffer) - log_ram_length)) {
        log_ram_length = sizeof(log_ram_buffer) - 1;
        log_ram_buffer[log_ram_length] = '\0';
        log_ram_truncated = true;
        mutex_exit(&log_mutex);
        return;
    }
    log_ram_length += (size_t)written;

    if (log_ram_length + 1 >= sizeof(log_ram_buffer)) {
        log_ram_length = sizeof(log_ram_buffer) - 1;
        log_ram_buffer[log_ram_length] = '\0';
        log_ram_truncated = true;
        mutex_exit(&log_mutex);
        return;
    }

    log_ram_buffer[log_ram_length++] = '\n';
    log_ram_buffer[log_ram_length] = '\0';

    mutex_exit(&log_mutex);
}

const char *savedata_get_log()
{
    return (const char *)(XIP_BASE + LOG_SECTOR_OFFSET);
}

size_t savedata_log_size()
{
    const uint8_t *data = (const uint8_t *)savedata_get_log();
    size_t size = 0;
    while ((size < LOG_SECTOR_SIZE) && (data[size] != 0xff) && (data[size] != '\0')) {
        size++;
    }
    return size;
}

void savedata_log_save()
{
    request_log_persist = true;
}

static void log_persist()
{
    static uintptr_t param[2];

    mutex_enter_blocking(&log_mutex);
    if (log_persisted) {
        mutex_exit(&log_mutex);
        return;
    }

    if (log_ram_length < sizeof(log_ram_buffer)) {
        log_ram_buffer[log_ram_length] = '\0';
    }

    if (log_ram_length + 1 < sizeof(log_ram_buffer)) {
        memset(&log_ram_buffer[log_ram_length + 1], 0xff,
               sizeof(log_ram_buffer) - (log_ram_length + 1));
    }
    log_persisted = true;
    mutex_exit(&log_mutex);

    param[0] = (uintptr_t)log_ram_buffer;
    param[1] = LOG_SECTOR_SIZE;

    printf("Program Log %08x ", LOG_SECTOR_OFFSET);
    if (flash_safe_execute(do_write_log, param, 1000) != PICO_OK) {
        printf("Failed!\n");
    } else {
        printf("Done.\n");
    }
}

void savedata_loop()
{
    if (requesting_save && (time_us_64() - requesting_time > SAVE_TIMEOUT_US)) {
        requesting_save = false;
        if (memcmp(&old_data, &new_data, sizeof(old_data)) == 0) {
            return;
        }
        save_program();
    }

    if (!log_persisted && request_log_persist) {
        log_persist();
    }
}
