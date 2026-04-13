/*
 * PS4 Key import and de-serialization
 * WHowe <github.com/whowechina>
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

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

static bool pem_is_valid(const char *pem)
{
    return strstr(pem, "-----BEGIN RSA PRIVATE KEY-----") &&
           strstr(pem, "-----END RSA PRIVATE KEY-----");
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
        (key->pem_len <= 1) || (key->pem_len > (PS4KEY_PEM_MAX_LENGTH + 1)) ||
        (key->sig_len == 0) || (key->sig_len > PS4KEY_SIG_MAX_LENGTH)) {
        set_error(error, "Serialized part lengths are invalid.");
        return false;
    }

    size_t payload_len = key->serial_len + key->pem_len + key->sig_len;
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

    const uint8_t *pem_ptr = key->payload + key->serial_len;
    if (pem_ptr[key->pem_len - 1] != '\0') {
        set_error(error, "PEM data is not null terminated.");
        return false;
    }

    if (!pem_is_valid((const char *)pem_ptr)) {
        set_error(error, "PEM content does not look like an RSA private key.");
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
    size_t payload_len = src->serial_len + src->pem_len + src->sig_len;
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
    if (key == NULL) {
        return NULL;
    }
    return (const char *)(key->payload + key->serial_len);
}

const uint8_t *ps4key_get_sig(const ps4key_t *key)
{
    if (key == NULL) {
        return NULL;
    }
    return key->payload + key->serial_len + key->pem_len;
}

size_t ps4key_get_sig_len(const ps4key_t *key)
{
    if (key == NULL) {
        return 0;
    }
    return key->sig_len;
}
