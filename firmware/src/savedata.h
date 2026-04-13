/*
 * Controller Flash Save and Load
 * WHowe <github.com/whowechina>
 */

#ifndef SAVEDATA_H
#define SAVEDATA_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/multicore.h"

/* It's safer to lock other I/O ops during saving, so we need a locker */
void savedata_init(uint32_t magic);

void savedata_loop();

void *savedata_alloc(size_t size, void *def, void (*after_load)());
void savedata_request(bool immediately);

void *savedata_get_global();
void savedata_write_global(const void *data, size_t size);
void savedata_clear_global();
size_t savedata_global_size();

#endif
