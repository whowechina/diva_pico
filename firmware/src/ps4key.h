/*
 * PS4 Key import and de-serialization
 * WHowe <github.com/whowechina>
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
#define PS4KEY_SIG_LENGTH 256
#define PS4KEY_RSA_N_LENGTH 256
#define PS4KEY_RSA_E_LENGTH 4
#define PS4KEY_RSA_P_LENGTH 128
#define PS4KEY_RSA_Q_LENGTH 128
#define PS4KEY_PAYLOAD_MAX_LENGTH (PS4KEY_SERIAL_LENGTH + 1 + PS4KEY_SIG_LENGTH + \
                                  PS4KEY_RSA_N_LENGTH + PS4KEY_RSA_E_LENGTH + \
                                  PS4KEY_RSA_P_LENGTH + PS4KEY_RSA_Q_LENGTH)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t reserved;
    uint16_t serial_len;
    uint16_t sig_len;
    uint16_t rsa_n_len;
    uint16_t rsa_e_len;
    uint16_t rsa_p_len;
    uint16_t rsa_q_len;
    uint32_t crc32;
    uint8_t payload[PS4KEY_PAYLOAD_MAX_LENGTH];
} ps4key_t;

bool ps4key_parse_text(const char *text, ps4key_t *key, const char **error);
bool ps4key_key_valid(const ps4key_t *key);

const char *ps4key_get_serial(const ps4key_t *key);
const char *ps4key_get_pem(const ps4key_t *key);
const uint8_t *ps4key_get_sig(const ps4key_t *key);
size_t ps4key_get_sig_len(const ps4key_t *key);
const uint8_t *ps4key_get_rsa_n(const ps4key_t *key);
const uint8_t *ps4key_get_rsa_e(const ps4key_t *key);
const uint8_t *ps4key_get_rsa_p(const ps4key_t *key);
const uint8_t *ps4key_get_rsa_q(const ps4key_t *key);

#endif