/*
 * PS4 Key import and de-serialization
 * WHowe <github.com/whowechina>
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"

#include "ps4key.h"

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

static bool serial_is_valid(const uint8_t *serial, size_t size)
{
    if (size != (PS4KEY_SERIAL_LENGTH + 1)) {
        return false;
    }

    for (size_t i = 0; i < PS4KEY_SERIAL_LENGTH; i++) {
        if (!isdigit(serial[i])) {
            return false;
        }
    }
    if (serial[PS4KEY_SERIAL_LENGTH] != '\0') {
        return false;
    }

    return true;
}

static size_t key_payload_len(const ps4key_t *key)
{
    return key->serial_len + key->sig_len +
           key->rsa_n_len + key->rsa_e_len +
           key->rsa_p_len + key->rsa_q_len;
}

static size_t sig_offset(const ps4key_t *key)
{
    return key->serial_len;
}

static size_t rsa_n_offset(const ps4key_t *key)
{
    return sig_offset(key) + key->sig_len;
}

static size_t rsa_e_offset(const ps4key_t *key)
{
    return rsa_n_offset(key) + key->rsa_n_len;
}

static size_t rsa_p_offset(const ps4key_t *key)
{
    return rsa_e_offset(key) + key->rsa_e_len;
}

static size_t rsa_q_offset(const ps4key_t *key)
{
    return rsa_p_offset(key) + key->rsa_p_len;
}

static bool rebuild_pem(const ps4key_t *key, char *pem, size_t pem_size)
{
    mbedtls_mpi n;
    mbedtls_mpi e;
    mbedtls_mpi p;
    mbedtls_mpi q;
    mbedtls_mpi_init(&n);
    mbedtls_mpi_init(&e);
    mbedtls_mpi_init(&p);
    mbedtls_mpi_init(&q);

    int ret = mbedtls_mpi_read_binary(&n, ps4key_get_rsa_n(key), key->rsa_n_len);
    if (ret != 0) goto cleanup;
    ret = mbedtls_mpi_read_binary(&e, ps4key_get_rsa_e(key), key->rsa_e_len);
    if (ret != 0) goto cleanup;
    ret = mbedtls_mpi_read_binary(&p, ps4key_get_rsa_p(key), key->rsa_p_len);
    if (ret != 0) goto cleanup;
    ret = mbedtls_mpi_read_binary(&q, ps4key_get_rsa_q(key), key->rsa_q_len);
    if (ret != 0) goto cleanup;

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    ret = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (ret != 0) {
        mbedtls_pk_free(&pk);
        goto cleanup;
    }

    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk);
    mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);

    ret = mbedtls_rsa_import(rsa, &n, &p, &q, NULL, &e);
    if (ret == 0) {
        ret = mbedtls_rsa_complete(rsa);
    }
    if (ret == 0) {
        ret = mbedtls_pk_write_key_pem(&pk, (unsigned char *)pem, pem_size);
    }

    mbedtls_pk_free(&pk);

cleanup:
    mbedtls_mpi_free(&n);
    mbedtls_mpi_free(&e);
    mbedtls_mpi_free(&p);
    mbedtls_mpi_free(&q);
    return ret == 0;
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

    if ((key->serial_len != (PS4KEY_SERIAL_LENGTH + 1)) ||
        (key->sig_len != PS4KEY_SIG_LENGTH) ||
        (key->rsa_n_len != PS4KEY_RSA_N_LENGTH) ||
        (key->rsa_e_len == 0) || (key->rsa_e_len > PS4KEY_RSA_E_LENGTH) ||
        (key->rsa_p_len != PS4KEY_RSA_P_LENGTH) ||
        (key->rsa_q_len != PS4KEY_RSA_Q_LENGTH)) {
        set_error(error, "Serialized part lengths are invalid.");
        return false;
    }

    size_t payload_len = key_payload_len(key);
    if (payload_len > sizeof(key->payload)) {
        set_error(error, "Serialized length mismatch.");
        return false;
    }

    if (crc32_calc(key->payload, payload_len) != key->crc32) {
        set_error(error, "CRC32 mismatch.");
        return false;
    }

    if (!serial_is_valid(key->payload, key->serial_len)) {
        set_error(error, "Invalid serial.");
        return false;
    }

    size_t rsa_q_end = rsa_q_offset(key) + key->rsa_q_len;
    if (rsa_q_end != payload_len) {
        set_error(error, "Serialized layout mismatch.");
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
        set_error(error, "Missing PS4K1 prefix.");
        return false;
    }

    size_t decoded_len = 0;
    if (!base64_decode(text + strlen(PS4KEY_TEXT_PREFIX), decoded, sizeof(decoded), &decoded_len)) {
        set_error(error, "Base64 decode failed.");
        return false;
    }

    const size_t header_len = offsetof(ps4key_t, payload);
    if (decoded_len < header_len) {
        set_error(error, "Serialized data is too short.");
        return false;
    }
    const ps4key_t *src = (const ps4key_t *)decoded;
    size_t payload_len = key_payload_len(src);
    if (decoded_len != header_len + payload_len) {
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

const char *ps4key_get_serial(const ps4key_t *key)
{
    if (key == NULL) {
        return NULL;
    }
    return (const char *)key->payload;
}

const char *ps4key_get_pem(const ps4key_t *key)
{
    static char pem[2048] = {0};
    static uint32_t last_crc = 0;

    if ((key == NULL) || !ps4key_key_valid(key)) {
        return NULL;
    }

    if ((last_crc != key->crc32) || (pem[0] == '\0')) {
        memset(pem, 0, sizeof(pem));
        if (!rebuild_pem(key, pem, sizeof(pem))) {
            return NULL;
        }
        last_crc = key->crc32;
    }

    return pem;
}

const uint8_t *ps4key_get_sig(const ps4key_t *key)
{
    if (key == NULL) {
        return NULL;
    }
    return key->payload + sig_offset(key);
}

size_t ps4key_get_sig_len(const ps4key_t *key)
{
    if (key == NULL) {
        return 0;
    }
    return key->sig_len;
}

const uint8_t *ps4key_get_rsa_n(const ps4key_t *key)
{
    if (key == NULL) {
        return NULL;
    }
    return key->payload + rsa_n_offset(key);
}

const uint8_t *ps4key_get_rsa_e(const ps4key_t *key)
{
    if (key == NULL) {
        return NULL;
    }
    return key->payload + rsa_e_offset(key);
}

const uint8_t *ps4key_get_rsa_p(const ps4key_t *key)
{
    if (key == NULL) {
        return NULL;
    }
    return key->payload + rsa_p_offset(key);
}

const uint8_t *ps4key_get_rsa_q(const ps4key_t *key)
{
    if (key == NULL) {
        return NULL;
    }
    return key->payload + rsa_q_offset(key);
}
