/*
 * PS4 Key import and de-serialization
 * WHowe <github.com/whowechina>
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/time.h"

#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/bignum.h"

#include "ps4key.h"
#include "savedata.h"
#include "tusb.h"

#define PS4_AUTH_NONCE_SIZE 256
#define PS4_AUTH_BLOCK_SIZE 1064

static struct {
    bool rsa_ready;
    mbedtls_rsa_context rsa;

    uint8_t nonce_id;
    uint8_t curr_chunk;

    volatile bool request_sign;
    volatile uint8_t signed_nonce_id;

    uint8_t auth_buffer[PS4_AUTH_BLOCK_SIZE];
} auth_ctx;


static const uint8_t ps4_feat_02[] = {
    0xfe, 0xff, 0x0e, 0x00, 0x04, 0x00, 0xd4, 0x22,
    0x2a, 0xdd, 0xbb, 0x22, 0x5e, 0xdd, 0x81, 0x22,
    0x84, 0xdd, 0x1c, 0x02, 0x1c, 0x02, 0x85, 0x1f,
    0xb0, 0xe0, 0xc6, 0x20, 0xb5, 0xe0, 0xb1, 0x20,
    0x83, 0xdf, 0x0c, 0x00
};

static const uint8_t ps4_feat_03[] = {
    0x21, 0x27, 0x04, 0xcf, 0x00, 0x2c, 0x56,
    0x08, 0x00, 0x3d, 0x00, 0xe8, 0x03, 0x04, 0x00,
    0xff, 0x7f, 0x0d, 0x0d, 0x00, 0x00, 0x00, 0x00,
    0x0d, 0x84, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t ps4_feat_12[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x25, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t ps4_feat_a3[] = {
    0x4a, 0x75, 0x6e, 0x20, 0x20, 0x39, 0x20, 0x32,
    0x30, 0x31, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x31, 0x32, 0x3a, 0x33, 0x36, 0x3a, 0x34, 0x31,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x08, 0xb4, 0x01, 0x00, 0x00, 0x00,
    0x07, 0xa0, 0x10, 0x20, 0x00, 0xa0, 0x02, 0x00
};

static const uint8_t ps4_feat_f3[] = {
    0x00, 0x38, 0x38, 0x00, 0x00, 0x00, 0x00
};

static uint32_t crc32_calc(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffffu;

    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }

    return ~crc;
}

static int ps4_rng(void *ctx, uint8_t *output, size_t output_len)
{
    for (size_t i = 0; i < output_len; i++) {
        output[i] = rand();
    }
    return 0;
}

static void set_error(const char **error, const char *message)
{
    if (error) {
        *error = message;
    }
}

static bool serial_is_valid(const uint8_t *serial)
{
    for (size_t i = 0; i < sizeof(((ps4key_t *)0)->serial); i++) {
        if (!isdigit((unsigned char)serial[i])) {
            return false;
        }
    }

    return true;
}

static bool ps4key_validate(const ps4key_t *key, const char **error)
{
    if (key == NULL) {
        set_error(error, "Missing input.");
        return false;
    }

    if ((key->magic != PS4KEY_STORAGE_MAGIC) ||
        (key->version != PS4KEY_STORAGE_VERSION)) {
        set_error(error, "Unexpected serialization header.");
        return false;
    }

    int payload_len = sizeof(ps4key_t) - offsetof(ps4key_t, serial);
    if (crc32_calc(key->serial, payload_len) != key->crc32) {
        set_error(error, "CRC32 mismatch.");
        return false;
    }

    if (!serial_is_valid(key->serial)) {
        set_error(error, "Invalid serial.");
        return false;
    }

    return true;
}

static const ps4key_t *ps4key_load_key()
{
    const ps4key_t *key = (const ps4key_t *)savedata_get_global();
    if (!ps4key_key_valid(key)) {
        return NULL;
    }
    return key;
}

void ps4key_init()
{
    auth_ctx.request_sign = false;
    auth_ctx.signed_nonce_id = 0;
}

void ps4key_job_loop()
{
    if (auth_ctx.request_sign) {
        ps4key_process_auth();
    }
}

static void ps4key_free_rsa()
{
    if (!auth_ctx.rsa_ready) {
        return;
    }
    mbedtls_rsa_free(&auth_ctx.rsa);
    memset(&auth_ctx.rsa, 0, sizeof(auth_ctx.rsa));
    auth_ctx.rsa_ready = false;
}

static bool ps4key_prepare_rsa()
{
    const ps4key_t *key = ps4key_load_key();
    if (key == NULL) {
        ps4key_free_rsa();
        return false;
    }

    if (auth_ctx.rsa_ready) {
        return true;
    }

    mbedtls_mpi n, e, d, p, q;
    mbedtls_mpi_init(&n);
    mbedtls_mpi_init(&e);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&p);
    mbedtls_mpi_init(&q);

    int ret = mbedtls_mpi_read_binary(&n, key->rsa_n, sizeof(key->rsa_n));
    if (ret == 0) {
        ret = mbedtls_mpi_read_binary(&e, key->rsa_e, sizeof(key->rsa_e));
    }
    if (ret == 0) {
        ret = mbedtls_mpi_read_binary(&d, key->rsa_d, sizeof(key->rsa_d));
    }
    if (ret == 0) {
        ret = mbedtls_mpi_read_binary(&p, key->rsa_p, sizeof(key->rsa_p));
    }
    if (ret == 0) {
        ret = mbedtls_mpi_read_binary(&q, key->rsa_q, sizeof(key->rsa_q));
    }

    if (ret == 0) {
        mbedtls_rsa_init(&auth_ctx.rsa);
        mbedtls_rsa_set_padding(&auth_ctx.rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
        ret = mbedtls_rsa_import(&auth_ctx.rsa, &n, &p, &q, &d, &e);
    }

    if (ret == 0) {
        ret = mbedtls_rsa_complete(&auth_ctx.rsa);
    }

    mbedtls_mpi_free(&n);
    mbedtls_mpi_free(&e);
    mbedtls_mpi_free(&d);
    mbedtls_mpi_free(&p);
    mbedtls_mpi_free(&q);

    if (ret != 0) {
        ps4key_free_rsa();
        return false;
    }

    srand(0);
    auth_ctx.rsa_ready = true;
    return true;
}

static int get_feat_temp(uint8_t report_id, const uint8_t **temp)
{
    switch (report_id) {
        case 0x02:
            *temp = ps4_feat_02;
            return sizeof(ps4_feat_02);
        case 0x03:
            *temp = ps4_feat_03;
            return sizeof(ps4_feat_03);
        case 0x12:
            *temp = ps4_feat_12;
            return sizeof(ps4_feat_12);
        case 0xa3:
            *temp = ps4_feat_a3;
            return sizeof(ps4_feat_a3);
        case 0xf3:
            *temp = ps4_feat_f3;
            return sizeof(ps4_feat_f3);
        default:
            *temp = NULL;
            return -1;
    }
}

static int base64_value(char c)
{
    if ((c >= 'A') && (c <= 'Z')) {
        return c - 'A';
    }
    if ((c >= 'a') && (c <= 'z')) {
        return c - 'a' + 26;
    }
    if ((c >= '0') && (c <= '9')) {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    if (c == '=') {
        return -2;
    }
    return -1;
}

static bool base64_decode(const char *input, uint8_t *output,
                          size_t output_size, size_t *output_len)
{
    size_t input_len = strlen(input);
    size_t written = 0;

    if ((input_len == 0) || ((input_len % 4) != 0)) {
        return false;
    }

    for (size_t i = 0; i < input_len; i += 4) {
        int vals[4];
        for (int j = 0; j < 4; j++) {
            vals[j] = base64_value(input[i + j]);
            if (vals[j] == -1) {
                return false;
            }
        }

        if ((vals[0] < 0) || (vals[1] < 0)) {
            return false;
        }
        if ((vals[2] == -2) && (vals[3] != -2)) {
            return false;
        }
        if ((vals[2] == -2) || (vals[3] == -2)) {
            if (i + 4 != input_len) {
                return false;
            }
        }

        if (written >= output_size) {
            return false;
        }
        output[written++] = (vals[0] << 2) | (vals[1] >> 4);

        if (vals[2] == -2) {
            break;
        }
        if (written >= output_size) {
            return false;
        }
        output[written++] = ((vals[1] & 0x0f) << 4) | (vals[2] >> 2);

        if (vals[3] == -2) {
            break;
        }
        if (written >= output_size) {
            return false;
        }
        output[written++] = ((vals[2] & 0x03) << 6) | vals[3];
    }

    *output_len = written;
    return true;
}

bool ps4key_parse_text(const char *text, ps4key_t *key, const char **error)
{
    static uint8_t decoded[sizeof(ps4key_t)];

    if ((text == NULL) || (key == NULL)) {
        set_error(error, "Missing input.");
        return false;
    }

    if (strncmp(text, PS4KEY_TEXT_PREFIX, strlen(PS4KEY_TEXT_PREFIX)) != 0) {
        set_error(error, "Missing PS4KX prefix.");
        return false;
    }

    size_t decoded_len = 0;
    if (!base64_decode(text + strlen(PS4KEY_TEXT_PREFIX), decoded, sizeof(decoded), &decoded_len)) {
        set_error(error, "Base64 decode failed.");
        return false;
    }

    if (decoded_len != sizeof(ps4key_t)) {
        set_error(error, "Serialized length mismatch.");
        return false;
    }

    memset(key, 0, sizeof(*key));
    memcpy(key, decoded, decoded_len);

    return ps4key_validate(key, error);
}

bool ps4key_key_valid(const ps4key_t *key)
{
    return ps4key_validate(key, NULL);
}

void ps4key_process_auth()
{
    uint8_t nonce_block[PS4_AUTH_NONCE_SIZE];
    uint8_t nonce_id = 0;

    nonce_id = auth_ctx.nonce_id;
    memcpy(nonce_block, auth_ctx.auth_buffer, sizeof(nonce_block));

    if (!ps4key_prepare_rsa()) {
        goto out;
    }

    const ps4key_t *key = ps4key_load_key();
    if (key == NULL) {
        goto out;
    }

    uint8_t nonce_hash[32];
    if (mbedtls_sha256(nonce_block, sizeof(nonce_block), nonce_hash, 0) != 0) {
        goto out;
    }

    uint8_t signed_payload[PS4_AUTH_BLOCK_SIZE] = {0};

    if (mbedtls_rsa_rsassa_pss_sign(&auth_ctx.rsa,
                                    ps4_rng,
                                    NULL,
                                    MBEDTLS_MD_SHA256,
                                    sizeof(nonce_hash),
                                    nonce_hash,
                                    signed_payload) != 0) {
        goto out;
    }

    size_t offset = 256;
    uint8_t serial_bin[16] = {0};
    for (int i = 0; i < 8; i++) {
        uint8_t hi = (uint8_t)(key->serial[i * 2    ] - '0');
        uint8_t lo = (uint8_t)(key->serial[i * 2 + 1] - '0');
        serial_bin[8 + i] = (uint8_t)((hi << 4) | lo);
    }
    memcpy(&signed_payload[offset], serial_bin, 16);
    offset += 16;

    if (mbedtls_rsa_export_raw(&auth_ctx.rsa,
                               &signed_payload[offset], 256,
                               NULL, 0,
                               NULL, 0,
                               NULL, 0,
                               &signed_payload[offset + 256], 256) != 0) {
        goto out;
    }
    offset += 512;

    memcpy(&signed_payload[offset], key->signature, sizeof(key->signature));
    offset += sizeof(key->signature);
    memset(&signed_payload[offset], 0, PS4_AUTH_BLOCK_SIZE - offset);

    memcpy(auth_ctx.auth_buffer, signed_payload, sizeof(auth_ctx.auth_buffer));
    auth_ctx.curr_chunk = 0;
    auth_ctx.signed_nonce_id = nonce_id;

out:
    auth_ctx.request_sign = false;
}

static uint16_t f1_get_signature(uint8_t *buffer, uint16_t reqlen)
{
    uint8_t data[64] = {0};
    uint32_t crc32;
    uint8_t nonce_id;
    uint8_t chunk;
    bool done;

    uint16_t resp_len = reqlen > 63 ? 63 : reqlen;

    data[0] = 0xf1;
    data[1] = auth_ctx.nonce_id;
    data[2] = auth_ctx.curr_chunk;
    data[3] = 0;
    memcpy(&data[4], &auth_ctx.auth_buffer[auth_ctx.curr_chunk * 56], 56);
    crc32 = crc32_calc(data, 60);
    memcpy(&data[60], &crc32, sizeof(crc32));

    memcpy(buffer, &data[1], resp_len);

    nonce_id = auth_ctx.nonce_id;
    chunk = auth_ctx.curr_chunk;

    auth_ctx.curr_chunk++;
    if (auth_ctx.curr_chunk == 19) {
        auth_ctx.curr_chunk = 0;
    }
    done = (chunk == 18);

    if (chunk == 0) {
        savedata_logf("ps4 get sign begin id=%u", nonce_id);
    }
    if (done) {
        savedata_logf("ps4 get sign done id=%u", nonce_id);
    }
    return resp_len;
}

static uint16_t f2_check_ready(uint8_t *buffer, uint16_t reqlen)
{
    struct {
        uint8_t report_id;
        uint8_t nonce_id;
        uint8_t ready_flag;
        uint8_t data[9];
        uint32_t crc32;
    } __attribute__((packed)) frame;

    uint16_t resp_len = reqlen > 15 ? 15 : reqlen;

    bool ready = (!auth_ctx.request_sign) &&
                 (auth_ctx.signed_nonce_id == auth_ctx.nonce_id);

    frame.report_id = 0xf2;
    frame.nonce_id = auth_ctx.nonce_id;
    frame.ready_flag = ready ? 0 : 16;

    frame.crc32 = crc32_calc((const uint8_t *)&frame, 12);

    memcpy(buffer, &frame.nonce_id, resp_len);

    savedata_logf("ps4 check sign id=%u ready=%u", frame.nonce_id, ready ? 1u : 0u);

    return resp_len;
}

static uint16_t f3_auth_reset(uint8_t *buffer, uint16_t reqlen)
{
    uint16_t resp_len = sizeof(ps4_feat_f3);
    if (resp_len > reqlen) {
        resp_len = reqlen;
    }
    memcpy(buffer, ps4_feat_f3, resp_len);

    auth_ctx.nonce_id = 0;
    memset(auth_ctx.auth_buffer, 0, sizeof(auth_ctx.auth_buffer));

    savedata_logf("ps4 auth reset");
    return resp_len;
}

uint16_t ps4key_get_report(uint8_t report_id, uint8_t report_type,
                           uint8_t *buffer, uint16_t reqlen)
{
    if (report_type != HID_REPORT_TYPE_FEATURE) {
        savedata_logf("ps4 get unknown feat=%02x", report_id);
        memset(buffer, 0, reqlen);
        return reqlen;
    }

    if (report_id == 0xf1) {
        return f1_get_signature(buffer, reqlen);
    }

    if (report_id == 0xf2) {
        return f2_check_ready(buffer, reqlen);
    }

    if (report_id == 0xf3) {
        return f3_auth_reset(buffer, reqlen);
    }

    const uint8_t *resp = NULL;
    int resp_len = get_feat_temp(report_id, &resp);
    if (resp_len == -1) {
        savedata_logf("ps4 get unhandled feat id=%02x", report_id);
        memset(buffer, 0, reqlen);
        return reqlen;
    }

    savedata_logf("ps4 get feat id=%02x, len=%u", report_id, resp_len);

    if (resp_len > reqlen) {
        resp_len = reqlen;
    }

    memset(buffer, 0, resp_len);
    if (resp != NULL) {
        memcpy(buffer, resp, resp_len);
    }
    return resp_len;
}

static void f0_set_nonce(uint8_t report_id, uint8_t const *buffer, uint16_t bufsize)
{
    if (auth_ctx.request_sign) {
        return;
    }

    if (bufsize != 63) {
        savedata_logf("ps4 nonce bad-len=%u", bufsize);
        return;
    }

    struct {
        uint8_t report_id;
        uint8_t nonce_id;
        uint8_t nonce_page;
        uint8_t reserved;
        uint8_t data[56];
        uint32_t crc32;
    } __attribute__((packed)) frame;

    frame.report_id = report_id;
    memcpy(&frame.nonce_id, buffer, 63);

    if (crc32_calc((const uint8_t *)&frame, 60) != frame.crc32) {
        savedata_logf("ps4 nonce crc-error");
        return;
    }

    if (frame.nonce_page > 4) {
        savedata_logf("ps4 nonce bad-page=%u", frame.nonce_page);
        return;
    }

    if (frame.nonce_page == 0) {
        savedata_logf("ps4 nonce begin id=%u", frame.nonce_id);
        auth_ctx.nonce_id = frame.nonce_id;
        auth_ctx.curr_chunk = 0;
        memset(auth_ctx.auth_buffer, 0, sizeof(auth_ctx.auth_buffer));
    } else if (frame.nonce_id != auth_ctx.nonce_id) {
        savedata_logf("ps4 nonce id-mismatch got=%u cur=%u", frame.nonce_id, auth_ctx.nonce_id);
        return;
    }

    if (frame.nonce_page == 4) {
        memcpy(&auth_ctx.auth_buffer[frame.nonce_page * 56], frame.data, 32);
        auth_ctx.request_sign = true;
        savedata_logf("ps4 nonce complete id=%u queued=1", frame.nonce_id);
    } else {
        memcpy(&auth_ctx.auth_buffer[frame.nonce_page * 56], frame.data, 56);
    }
}

void ps4key_set_report(uint8_t report_id, uint8_t report_type,
                       uint8_t const *buffer, uint16_t bufsize)
{
    if (report_type != HID_REPORT_TYPE_FEATURE) {
        return;
    }

    if (report_id != 0xf0) {
        savedata_logf("ps4 set unhandled feat=%02x len=%u", report_id, bufsize);
        return;
    }

    f0_set_nonce(report_id, buffer, bufsize);
}
