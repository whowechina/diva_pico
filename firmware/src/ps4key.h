/*
 * PS4 key serialization support
 */

#ifndef PS4KEY_H
#define PS4KEY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PS4KEY_TEXT_PREFIX "PS4K1"
#define PS4KEY_STORAGE_MAGIC 0x4b345350u
#define PS4KEY_STORAGE_VERSION 1u
#define PS4KEY_SERIAL_LENGTH 16
#define PS4KEY_PEM_MAX_LENGTH 2048
#define PS4KEY_SIG_MAX_LENGTH 512

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t serial_len;
    uint16_t pem_len;
    uint16_t sig_len;
    uint32_t crc32;
    char serial[PS4KEY_SERIAL_LENGTH + 1];
    char pem[PS4KEY_PEM_MAX_LENGTH + 1];
    uint8_t sig[PS4KEY_SIG_MAX_LENGTH];
} ps4key_data_t;

void ps4key_clear_data(ps4key_data_t *data);
bool ps4key_parse_text(const char *text, ps4key_data_t *data, const char **error);

#endif