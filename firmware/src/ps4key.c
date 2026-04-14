/*
 * PS4 Key import and de-serialization
 * WHowe <github.com/whowechina>
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/time.h"
#include "pico/mutex.h"
#include "pico/util/queue.h"

#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/bignum.h"

#include "ps4key.h"
#include "savedata.h"
#include "tusb.h"

#define PS4_AUTH_NONCE_SIZE 256
#define PS4_AUTH_BLOCK_SIZE 1064

typedef enum {
    PS4_AUTH_IDLE = 0,
    PS4_AUTH_SIGN_REQUEST = 1,
    PS4_AUTH_SIGNED_READY = 2,
} ps4_auth_state_t;

typedef struct {
    mbedtls_rsa_context rsa;
    bool rsa_ready;
    uint8_t auth_buffer[PS4_AUTH_BLOCK_SIZE];
    uint8_t nonce_id;
    uint8_t current_nonce_id;
    uint8_t current_nonce_chunk;
    ps4_auth_state_t state;
} ps4_auth_ctx_t;

static ps4_auth_ctx_t ps4_auth;
static mutex_t ps4_auth_mutex;
static queue_t ps4_auth_queue;
static bool ps4_auth_async_ready;
static bool sign_job_pending;
static uint8_t ps4_output_report0[64];
static uint16_t ps4_output_report0_len;

#define PS4_AUTH_JOB_SIGN 1u

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

static uint8_t ps4_feat_13[22];
static uint8_t ps4_feat_14[16];
static uint8_t ps4_feat_15[44];
static bool ps4_feat_defaults_inited = false;

static void ps4key_init_feature_defaults(void)
{
    if (ps4_feat_defaults_inited) {
        return;
    }

    memset(ps4_feat_13, 0, sizeof(ps4_feat_13));
    memset(ps4_feat_14, 0, sizeof(ps4_feat_14));
    memset(ps4_feat_15, 0, sizeof(ps4_feat_15));
    ps4_feat_defaults_inited = true;
}

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

static inline int rng(void *ctx, unsigned char *output, size_t output_len)
{
    (void)ctx;
    (void)output_len;
    output[0] = (unsigned char)rand();
    return 0;
}

typedef struct {
    uint32_t state;
} ps4key_bench_rng_t;

static int bench_rng(void *ctx, unsigned char *output, size_t output_len)
{
    ps4key_bench_rng_t *state = (ps4key_bench_rng_t *)ctx;
    for (size_t i = 0; i < output_len; i++) {
        state->state = state->state * 1664525u + 1013904223u;
        output[i] = (unsigned char)(state->state >> 24);
    }
    return 0;
}

static bool bytes_have_nonzero(const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        if (data[i] != 0) {
            return true;
        }
    }
    return false;
}

static const char *bool_text(bool value)
{
    return value ? "yes" : "no";
}

static void print_sig_prefix(const char *label, const uint8_t *sig, size_t size)
{
    size_t prefix = size < 8 ? size : 8;
    printf("  %s", label);
    for (size_t i = 0; i < prefix; i++) {
        printf("%02x", sig[i]);
    }
    printf("\n");
}

static void dump_rsa_ctx_debug(const char *label, const mbedtls_rsa_context *rsa)
{
    mbedtls_mpi n;
    mbedtls_mpi p;
    mbedtls_mpi q;
    mbedtls_mpi d;
    mbedtls_mpi e;
    mbedtls_mpi dp;
    mbedtls_mpi dq;
    mbedtls_mpi qp;
    mbedtls_mpi_init(&n);
    mbedtls_mpi_init(&p);
    mbedtls_mpi_init(&q);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&e);
    mbedtls_mpi_init(&dp);
    mbedtls_mpi_init(&dq);
    mbedtls_mpi_init(&qp);

    int export_ret = mbedtls_rsa_export(rsa, &n, &p, &q, &d, &e);
    int crt_ret = mbedtls_rsa_export_crt(rsa, &dp, &dq, &qp);
    printf("  %s ctx: export=%d crt_export=%d Nbits=%lu P=%s Q=%s D=%s dP=%s dQ=%s qInv=%s\n",
           label,
           export_ret,
           crt_ret,
           (unsigned long)mbedtls_mpi_bitlen(&n),
           bool_text(mbedtls_mpi_cmp_int(&p, 0) != 0),
           bool_text(mbedtls_mpi_cmp_int(&q, 0) != 0),
           bool_text(mbedtls_mpi_cmp_int(&d, 0) != 0),
           bool_text(mbedtls_mpi_cmp_int(&dp, 0) != 0),
           bool_text(mbedtls_mpi_cmp_int(&dq, 0) != 0),
           bool_text(mbedtls_mpi_cmp_int(&qp, 0) != 0));

    mbedtls_mpi_free(&n);
    mbedtls_mpi_free(&p);
    mbedtls_mpi_free(&q);
    mbedtls_mpi_free(&d);
    mbedtls_mpi_free(&e);
    mbedtls_mpi_free(&dp);
    mbedtls_mpi_free(&dq);
    mbedtls_mpi_free(&qp);
}

static void  ps4key_log_get_f2_once(uint8_t nonce_id, ps4_auth_state_t state)
{
    static uint8_t last_nonce_id = 0xff;
    static ps4_auth_state_t last_state = 0xff;

    if ((last_nonce_id == nonce_id) && (last_state == state)) {
        return;
    }

    last_nonce_id = nonce_id;
    last_state = state;
    savedata_logf("ps4 get f2 id=%u ready=%u", nonce_id,
                  state == PS4_AUTH_SIGNED_READY ? 1u : 0u);
}

static const ps4key_t *ps4key_get_global_valid()
{
    const ps4key_t *key = (const ps4key_t *)savedata_get_global();
    if (!ps4key_key_valid(key)) {
        return NULL;
    }
    return key;
}

static void ps4key_enqueue_sign_job_locked(void)
{
    if (!ps4_auth_async_ready || sign_job_pending) {
        return;
    }

    const uint8_t job = PS4_AUTH_JOB_SIGN;
    if (queue_try_add(&ps4_auth_queue, &job)) {
        sign_job_pending = true;
    }
}

void ps4key_async_init(void)
{
    mutex_init(&ps4_auth_mutex);
    queue_init(&ps4_auth_queue, sizeof(uint8_t), 4);
    ps4_auth_async_ready = true;
    sign_job_pending = false;
}

void ps4key_core1_loop(void)
{
    uint8_t job = 0;

    while (1) {
        queue_remove_blocking(&ps4_auth_queue, &job);
        if (job == PS4_AUTH_JOB_SIGN) {
            ps4key_process_auth();
        }
    }
}

static void ps4key_free_rsa()
{
    if (!ps4_auth.rsa_ready) {
        return;
    }
    mbedtls_rsa_free(&ps4_auth.rsa);
    memset(&ps4_auth.rsa, 0, sizeof(ps4_auth.rsa));
    ps4_auth.rsa_ready = false;
}

static bool ps4key_prepare_rsa()
{
    const ps4key_t *key = ps4key_get_global_valid();
    if (key == NULL) {
        ps4key_free_rsa();
        return false;
    }

    if (ps4_auth.rsa_ready) {
        return true;
    }

    mbedtls_mpi n, e, d;
    mbedtls_mpi_init(&n);
    mbedtls_mpi_init(&e);
    mbedtls_mpi_init(&d);

    int ret = mbedtls_mpi_read_binary(&n, key->rsa_n, sizeof(key->rsa_n));
    if (ret == 0) {
        ret = mbedtls_mpi_read_binary(&e, key->rsa_e, sizeof(key->rsa_e));
    }
    if (ret == 0) {
        ret = mbedtls_mpi_read_binary(&d, key->rsa_d, sizeof(key->rsa_d));
    }

    if (ret == 0) {
        mbedtls_rsa_init(&ps4_auth.rsa);
        mbedtls_rsa_set_padding(&ps4_auth.rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
        ret = mbedtls_rsa_import(&ps4_auth.rsa, &n, NULL, NULL, &d, &e);
    }
    if (ret == 0) {
        ret = mbedtls_rsa_complete(&ps4_auth.rsa);
    }

    mbedtls_mpi_free(&n);
    mbedtls_mpi_free(&e);
    mbedtls_mpi_free(&d);

    if (ret != 0) {
        ps4key_free_rsa();
        return false;
    }

    srand(0);
    ps4_auth.rsa_ready = true;
    return true;
}

static uint16_t ps4_feature_size(uint8_t report_id)
{
    switch (report_id) {
        case 0x02: return sizeof(ps4_feat_02);
        case 0x03: return sizeof(ps4_feat_03);
        case 0x08: return 3;
        case 0x10: return 4;
        case 0x11: return 2;
        case 0x12: return sizeof(ps4_feat_12);
        case 0x13: return sizeof(ps4_feat_13);
        case 0x14: return sizeof(ps4_feat_14);
        case 0x15: return sizeof(ps4_feat_15);
        case 0x80: return 6;
        case 0x81: return 6;
        case 0x82: return 5;
        case 0x83: return 1;
        case 0x84: return 4;
        case 0x85: return 6;
        case 0x86: return 6;
        case 0x87: return 35;
        case 0x88: return 34;
        case 0x89: return 2;
        case 0x90: return 5;
        case 0x91: return 3;
        case 0x92: return 3;
        case 0x93: return 12;
        case 0xa0: return 6;
        case 0xa1: return 1;
        case 0xa2: return 1;
        case 0xa3: return sizeof(ps4_feat_a3);
        case 0xa4: return 13;
        case 0xa5: return 21;
        case 0xa6: return 21;
        case 0xa7: return 1;
        case 0xa8: return 1;
        case 0xa9: return 8;
        case 0xaa: return 1;
        case 0xab: return 57;
        case 0xac: return 57;
        case 0xad: return 11;
        case 0xaf: return 2;
        case 0xb0: return 63;
        case 0xf0: return 63;
        case 0xf1: return 63;
        case 0xf2: return 15;
        case 0xf3: return sizeof(ps4_feat_f3);
        default: return 0;
    }
}

static const uint8_t *ps4_feature_data(uint8_t report_id)
{
    switch (report_id) {
        case 0x02: return ps4_feat_02;
        case 0x03: return ps4_feat_03;
        case 0x12: return ps4_feat_12;
        case 0x13: return ps4_feat_13;
        case 0x14: return ps4_feat_14;
        case 0x15: return ps4_feat_15;
        case 0xa3: return ps4_feat_a3;
        case 0xf3: return ps4_feat_f3;
        default: return NULL;
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

static bool serial_is_valid(const uint8_t *serial)
{
    for (size_t i = 0; i < sizeof(((ps4key_t *)0)->serial); i++) {
        if (!isdigit((unsigned char)serial[i])) {
            return false;
        }
    }

    return true;
}

static void set_error(const char **error, const char *message)
{
    if (error) {
        *error = message;
    }
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

    if (crc32_calc(key->serial, PS4KEY_PAYLOAD_LENGTH) != key->crc32) {
        set_error(error, "CRC32 mismatch.");
        return false;
    }

    if (!serial_is_valid(key->serial)) {
        set_error(error, "Invalid serial.");
        return false;
    }

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

    mutex_enter_blocking(&ps4_auth_mutex);
    if (ps4_auth.state != PS4_AUTH_SIGN_REQUEST) {
        sign_job_pending = false;
        mutex_exit(&ps4_auth_mutex);
        return;
    }
    nonce_id = ps4_auth.current_nonce_id;
    memcpy(nonce_block, ps4_auth.auth_buffer, sizeof(nonce_block));
    sign_job_pending = false;
    mutex_exit(&ps4_auth_mutex);

    if (!ps4key_prepare_rsa()) {
        mutex_enter_blocking(&ps4_auth_mutex);
        if ((ps4_auth.state == PS4_AUTH_SIGN_REQUEST) && (ps4_auth.current_nonce_id == nonce_id)) {
            ps4_auth.state = PS4_AUTH_IDLE;
        }
        mutex_exit(&ps4_auth_mutex);
        return;
    }

    const ps4key_t *key = ps4key_get_global_valid();
    if (key == NULL) {
        mutex_enter_blocking(&ps4_auth_mutex);
        if ((ps4_auth.state == PS4_AUTH_SIGN_REQUEST) && (ps4_auth.current_nonce_id == nonce_id)) {
            ps4_auth.state = PS4_AUTH_IDLE;
        }
        mutex_exit(&ps4_auth_mutex);
        return;
    }

    uint8_t nonce_hash[32];
    if (mbedtls_sha256(nonce_block, sizeof(nonce_block), nonce_hash, 0) != 0) {
        mutex_enter_blocking(&ps4_auth_mutex);
        if ((ps4_auth.state == PS4_AUTH_SIGN_REQUEST) && (ps4_auth.current_nonce_id == nonce_id)) {
            ps4_auth.state = PS4_AUTH_IDLE;
        }
        mutex_exit(&ps4_auth_mutex);
        return;
    }

    uint8_t signed_payload[PS4_AUTH_BLOCK_SIZE] = {0};

    if (mbedtls_rsa_rsassa_pss_sign(&ps4_auth.rsa,
                                    rng,
                                    NULL,
                                    MBEDTLS_MD_SHA256,
                                    sizeof(nonce_hash),
                                    nonce_hash,
                                    signed_payload) != 0) {
        mutex_enter_blocking(&ps4_auth_mutex);
        if ((ps4_auth.state == PS4_AUTH_SIGN_REQUEST) && (ps4_auth.current_nonce_id == nonce_id)) {
            ps4_auth.state = PS4_AUTH_IDLE;
        }
        mutex_exit(&ps4_auth_mutex);
        return;
    }

    size_t offset = 256;
    memcpy(&signed_payload[offset], key->serial, sizeof(key->serial));
    offset += sizeof(key->serial);

    if (mbedtls_rsa_export_raw(&ps4_auth.rsa,
                               &signed_payload[offset], 256,
                               NULL, 0,
                               NULL, 0,
                               NULL, 0,
                               &signed_payload[offset + 256], 256) != 0) {
        mutex_enter_blocking(&ps4_auth_mutex);
        if ((ps4_auth.state == PS4_AUTH_SIGN_REQUEST) && (ps4_auth.current_nonce_id == nonce_id)) {
            ps4_auth.state = PS4_AUTH_IDLE;
        }
        mutex_exit(&ps4_auth_mutex);
        return;
    }
    offset += 512;

    memcpy(&signed_payload[offset], key->signature, sizeof(key->signature));
    offset += sizeof(key->signature);
    memset(&signed_payload[offset], 0, PS4_AUTH_BLOCK_SIZE - offset);

    mutex_enter_blocking(&ps4_auth_mutex);
    if ((ps4_auth.state == PS4_AUTH_SIGN_REQUEST) && (ps4_auth.current_nonce_id == nonce_id)) {
        memcpy(ps4_auth.auth_buffer, signed_payload, sizeof(ps4_auth.auth_buffer));
        ps4_auth.current_nonce_chunk = 0;
        ps4_auth.state = PS4_AUTH_SIGNED_READY;
    }
    mutex_exit(&ps4_auth_mutex);
}

void ps4key_reset_auth()
{
    mutex_enter_blocking(&ps4_auth_mutex);
    ps4_auth.nonce_id = 0;
    ps4_auth.current_nonce_id = 0;
    ps4_auth.current_nonce_chunk = 0;
    ps4_auth.state = PS4_AUTH_IDLE;
    sign_job_pending = false;
    memset(ps4_auth.auth_buffer, 0, sizeof(ps4_auth.auth_buffer));
    mutex_exit(&ps4_auth_mutex);

    if (ps4_auth_async_ready) {
        uint8_t dropped = 0;
        while (queue_try_remove(&ps4_auth_queue, &dropped)) {
        }
    }
}

uint16_t ps4key_get_report(uint8_t report_id, uint8_t report_type,
                           uint8_t *buffer, uint16_t reqlen)
{
    ps4key_init_feature_defaults();

    if (report_type != HID_REPORT_TYPE_FEATURE) {
        savedata_logf("ps4 get non-feature id=%02x", report_id);
        memset(buffer, 0, reqlen);
        return reqlen;
    }

    if (report_id == 0xf1) {
        uint8_t data[64] = {0};
        uint32_t crc32;
        uint8_t nonce_id;
        uint8_t chunk;
        bool done;

        mutex_enter_blocking(&ps4_auth_mutex);

        data[0] = 0xf1;
        data[1] = ps4_auth.current_nonce_id;
        data[2] = ps4_auth.current_nonce_chunk;
        data[3] = 0;
        memcpy(&data[4], &ps4_auth.auth_buffer[ps4_auth.current_nonce_chunk * 56], 56);
        crc32 = crc32_calc(data, 60);
        memcpy(&data[60], &crc32, sizeof(crc32));

        memcpy(buffer, &data[1], 63);

        nonce_id = ps4_auth.current_nonce_id;
        chunk = ps4_auth.current_nonce_chunk;

        ps4_auth.current_nonce_chunk++;
        if (ps4_auth.current_nonce_chunk == 19) {
            ps4_auth.current_nonce_chunk = 0;
            ps4_auth.state = PS4_AUTH_IDLE;
        }
        done = (chunk == 18);
        mutex_exit(&ps4_auth_mutex);

        if (chunk == 0) {
            savedata_logf("ps4 get f1 begin id=%u", nonce_id);
        }
        if (done) {
            savedata_logf("ps4 get f1 done id=%u", nonce_id);
        }
        return 63;
    }

    if (report_id == 0xf2) {
        uint8_t data[16] = {0};
        uint32_t crc32;
        uint8_t nonce_id;
        ps4_auth_state_t state;

        mutex_enter_blocking(&ps4_auth_mutex);

        data[0] = 0xf2;
        data[1] = ps4_auth.current_nonce_id;
        data[2] = (ps4_auth.state == PS4_AUTH_SIGNED_READY) ? 0 : 16;
        nonce_id = ps4_auth.current_nonce_id;
        state = ps4_auth.state;
        crc32 = crc32_calc(data, 12);
        memcpy(&data[12], &crc32, sizeof(crc32));

        memcpy(buffer, &data[1], 15);
        mutex_exit(&ps4_auth_mutex);

        ps4key_log_get_f2_once(nonce_id, state);
        return 15;
    }

    if (report_id == 0xf3) {
        uint16_t resp_len = sizeof(ps4_feat_f3);
        if (resp_len > reqlen) {
            resp_len = reqlen;
        }
        memcpy(buffer, ps4_feat_f3, resp_len);
        savedata_logf("ps4 get f3 reset");
        ps4key_reset_auth();
        return resp_len;
    }

    savedata_logf("ps4 get unhandled id=%02x", report_id);

    uint16_t resp_len = ps4_feature_size(report_id);
    const uint8_t *resp = ps4_feature_data(report_id);

    if (resp_len == 0) {
        resp_len = reqlen;
    }
    if (resp_len > reqlen) {
        resp_len = reqlen;
    }

    memset(buffer, 0, resp_len);
    if (resp != NULL) {
        memcpy(buffer, resp, resp_len);
    }
    return resp_len;
}

void ps4key_set_report(uint8_t report_id, uint8_t report_type,
                       uint8_t const *buffer, uint16_t bufsize)
{
    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        if (report_id == 0) {
            ps4_output_report0_len = bufsize > sizeof(ps4_output_report0) ?
                                     sizeof(ps4_output_report0) : bufsize;
            memcpy(ps4_output_report0, buffer, ps4_output_report0_len);
        }
        return;
    }

    if (report_type != HID_REPORT_TYPE_FEATURE) {
        return;
    }

    if (report_id != 0xf0) {
        savedata_logf("ps4 set feat=%02x len=%u", report_id, bufsize);
        return;
    }

    if (bufsize != 63) {
        savedata_logf("ps4 set f0 bad-len=%u", bufsize);
        return;
    }

    uint8_t send_buffer[64] = {0};
    send_buffer[0] = report_id;
    memcpy(&send_buffer[1], buffer, 63);

    uint32_t expected_crc = 0;
    memcpy(&expected_crc, &send_buffer[60], sizeof(expected_crc));
    if (crc32_calc(send_buffer, 60) != expected_crc) {
        savedata_logf("ps4 set f0 crc-error");
        return;
    }

    const uint8_t nonce_id = buffer[0];
    const uint8_t nonce_page = buffer[1];
    if (nonce_page > 4) {
        savedata_logf("ps4 set f0 bad-page=%u", nonce_page);
        return;
    }

    mutex_enter_blocking(&ps4_auth_mutex);

    if (nonce_page == 0) {
        savedata_logf("ps4 set f0 begin id=%u", nonce_id);
        ps4_auth.state = PS4_AUTH_IDLE;
        ps4_auth.current_nonce_id = nonce_id;
        ps4_auth.current_nonce_chunk = 0;
        sign_job_pending = false;
        memset(ps4_auth.auth_buffer, 0, sizeof(ps4_auth.auth_buffer));
    } else if (nonce_id != ps4_auth.current_nonce_id) {
        uint8_t current_nonce_id = ps4_auth.current_nonce_id;
        ps4_auth.state = PS4_AUTH_IDLE;
        sign_job_pending = false;
        mutex_exit(&ps4_auth_mutex);
        savedata_logf("ps4 set f0 id-mismatch got=%u cur=%u", nonce_id, current_nonce_id);
        return;
    }

    if (nonce_page == 4) {
        memcpy(&ps4_auth.auth_buffer[nonce_page * 56], &send_buffer[4], 32);
        ps4_auth.nonce_id = nonce_id;
        ps4_auth.state = PS4_AUTH_SIGN_REQUEST;
        ps4key_enqueue_sign_job_locked();
        savedata_logf("ps4 set f0 complete id=%u queued=1", nonce_id);
    } else {
        memcpy(&ps4_auth.auth_buffer[nonce_page * 56], &send_buffer[4], 56);
    }

    mutex_exit(&ps4_auth_mutex);
}

void ps4key_bench_sign(void)
{
    const ps4key_t *key = ps4key_get_global_valid();
    if (key == NULL) {
        printf("RSA sign benchmark: No valid key loaded.\n");
        return;
    }

    /* Prepare test nonce (256 bytes of sample data) */
    uint8_t test_nonce[PS4_AUTH_NONCE_SIZE];
    for (int i = 0; i < (int)sizeof(test_nonce); i++) {
        test_nonce[i] = (uint8_t)(i & 0xFF);
    }

    /* Compute hash of test nonce */
    uint8_t nonce_hash[32];
    if (mbedtls_sha256(test_nonce, sizeof(test_nonce), nonce_hash, 0) != 0) {
        printf("RSA sign benchmark: SHA256 failed.\n");
        return;
    }

    uint8_t sig_crt[256] = {0};
    uint8_t sig_noncrt[256] = {0};
    uint32_t time_crt = 0;
    uint32_t time_noncrt = 0;
    int crt_ret = -1;
    int noncrt_ret = -1;
    ps4key_bench_rng_t crt_rng = {.state = 0x13572468u};
    ps4key_bench_rng_t noncrt_rng = {.state = 0x13572468u};

    printf("RSA Sign Benchmark Debug:\n");
    printf("  Stored key version: V%u\n", key->version);
    printf("  Stored params: D=%s dP=%s dQ=%s qInv=%s\n",
           bool_text(bytes_have_nonzero(key->rsa_d, sizeof(key->rsa_d))),
           bool_text(bytes_have_nonzero(key->rsa_dp, sizeof(key->rsa_dp))),
           bool_text(bytes_have_nonzero(key->rsa_dq, sizeof(key->rsa_dq))),
           bool_text(bytes_have_nonzero(key->rsa_qinv, sizeof(key->rsa_qinv))));
    printf("  Note: benchmark uses deterministic salt so byte equality is meaningful here.\n");

    /* ============ CRT-accelerated signing ============ */
    mbedtls_rsa_context rsa_crt;
    mbedtls_rsa_init(&rsa_crt);
    mbedtls_rsa_set_padding(&rsa_crt, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);

    mbedtls_mpi n, e, p, q, d;
    mbedtls_mpi_init(&n);
    mbedtls_mpi_init(&e);
    mbedtls_mpi_init(&p);
    mbedtls_mpi_init(&q);
    mbedtls_mpi_init(&d);

    if (mbedtls_mpi_read_binary(&n, key->rsa_n, sizeof(key->rsa_n)) == 0 &&
        mbedtls_mpi_read_binary(&e, key->rsa_e, sizeof(key->rsa_e)) == 0 &&
        mbedtls_mpi_read_binary(&p, key->rsa_p, sizeof(key->rsa_p)) == 0 &&
        mbedtls_mpi_read_binary(&q, key->rsa_q, sizeof(key->rsa_q)) == 0 &&
        mbedtls_mpi_read_binary(&d, key->rsa_d, sizeof(key->rsa_d)) == 0 &&
        mbedtls_rsa_import(&rsa_crt, &n, &p, &q, &d, &e) == 0 &&
        mbedtls_rsa_complete(&rsa_crt) == 0) {
        dump_rsa_ctx_debug("CRT", &rsa_crt);
        uint32_t t0 = time_us_32();
        crt_ret = mbedtls_rsa_rsassa_pss_sign(&rsa_crt,
                                              bench_rng,
                                              &crt_rng,
                                              MBEDTLS_MD_SHA256,
                                              sizeof(nonce_hash),
                                              nonce_hash,
                                              sig_crt);
        uint32_t t1 = time_us_32();

        if (crt_ret == 0) {
            time_crt = t1 - t0;
        } else {
            printf("RSA sign benchmark: CRT signing failed (ret=%d).\n", crt_ret);
        }
    } else {
        printf("RSA sign benchmark: CRT setup failed.\n");
    }

    mbedtls_rsa_free(&rsa_crt);

    /* ============ Non-CRT signing (only N/D/E) ============ */
    mbedtls_rsa_context rsa_noncrt;
    mbedtls_rsa_init(&rsa_noncrt);
    mbedtls_rsa_set_padding(&rsa_noncrt, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);

    mbedtls_mpi n2, e2, d2;
    mbedtls_mpi_init(&n2);
    mbedtls_mpi_init(&e2);
    mbedtls_mpi_init(&d2);

    if (mbedtls_mpi_read_binary(&n2, key->rsa_n, sizeof(key->rsa_n)) == 0 &&
        mbedtls_mpi_read_binary(&e2, key->rsa_e, sizeof(key->rsa_e)) == 0 &&
        mbedtls_mpi_read_binary(&d2, key->rsa_d, sizeof(key->rsa_d)) == 0 &&
        mbedtls_rsa_import(&rsa_noncrt, &n2, NULL, NULL, &d2, &e2) == 0 &&
        mbedtls_rsa_complete(&rsa_noncrt) == 0) {
        dump_rsa_ctx_debug("Non-CRT", &rsa_noncrt);
        uint32_t t0 = time_us_32();
        noncrt_ret = mbedtls_rsa_rsassa_pss_sign(&rsa_noncrt,
                                                 bench_rng,
                                                 &noncrt_rng,
                                                 MBEDTLS_MD_SHA256,
                                                 sizeof(nonce_hash),
                                                 nonce_hash,
                                                 sig_noncrt);
        uint32_t t1 = time_us_32();

        if (noncrt_ret == 0) {
            time_noncrt = t1 - t0;
        } else {
            printf("RSA sign benchmark: Non-CRT signing failed (ret=%d).\n", noncrt_ret);
        }
    } else {
        printf("RSA sign benchmark: Non-CRT setup failed.\n");
    }

    mbedtls_rsa_free(&rsa_noncrt);
    mbedtls_mpi_free(&n);
    mbedtls_mpi_free(&e);
    mbedtls_mpi_free(&p);
    mbedtls_mpi_free(&q);
    mbedtls_mpi_free(&d);
    mbedtls_mpi_free(&n2);
    mbedtls_mpi_free(&e2);
    mbedtls_mpi_free(&d2);

    /* Compare results */
    int consistent = (crt_ret == 0 && noncrt_ret == 0 && memcmp(sig_crt, sig_noncrt, sizeof(sig_crt)) == 0) ? 1 : 0;

    printf("RSA Sign Benchmark Results:\n");
    printf("  CRT (with dP,dQ,qInv):     %10lu us\n", (unsigned long)time_crt);
    printf("  Non-CRT (N/D/E only):      %10lu us\n", (unsigned long)time_noncrt);
    if ((time_crt > 0) && (time_noncrt > 0)) {
        printf("  Speedup ratio (Non-CRT/CRT): %.2f x\n", (float)time_noncrt / (float)time_crt);
    }
    print_sig_prefix("CRT sig prefix:             ", sig_crt, sizeof(sig_crt));
    print_sig_prefix("Non-CRT sig prefix:         ", sig_noncrt, sizeof(sig_noncrt));
    printf("  Signature consistency: %s\n", consistent ? "MATCH ✓" : "MISMATCH ✗");
}
