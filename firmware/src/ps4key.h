/*
 * PS4 Key import and de-serialization
 * WHowe <github.com/whowechina>
 */

#ifndef PS4KEY_H
#define PS4KEY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PS4KEY_TEXT_PREFIX "PS4KX"
#define PS4KEY_STORAGE_MAGIC 0x4b345350u
#define PS4KEY_STORAGE_VERSION 3u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t reserved;
    uint32_t crc32;
    uint8_t serial[16];
    uint8_t signature[256];
    uint8_t rsa_n[256];
    uint8_t rsa_p[128];
    uint8_t rsa_q[128];
    uint8_t rsa_e[4];
    uint8_t rsa_d[256];   /* Private exponent for CRT */
    uint8_t rsa_dp[128];  /* d mod (p-1) for CRT */
    uint8_t rsa_dq[128];  /* d mod (q-1) for CRT */
    uint8_t rsa_qinv[128]; /* q^-1 mod p for CRT */
} ps4key_t;

bool ps4key_parse_text(const char *text, ps4key_t *key, const char **error);
bool ps4key_key_valid(const ps4key_t *key);

void ps4key_init(void);
void ps4key_job_loop();

void ps4key_process_auth(void);

uint16_t ps4key_get_report(uint8_t report_id, uint8_t report_type,
                           uint8_t *buffer, uint16_t reqlen);
void ps4key_set_report(uint8_t report_id, uint8_t report_type,
                       uint8_t const *buffer, uint16_t bufsize);

#endif