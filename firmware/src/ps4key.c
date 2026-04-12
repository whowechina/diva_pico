#include "ps4key.h"

#include <ctype.h>
#include <string.h>

typedef struct __attribute__((packed)) {
    uint8_t magic[4];
    uint8_t version;
    uint16_t serial_len;
    uint16_t pem_len;
    uint16_t sig_len;
    uint32_t crc32;
} ps4key_wire_header_t;

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
    if (size != PS4KEY_SERIAL_LENGTH) {
        return false;
    }

    for (size_t i = 0; i < size; i++) {
        if (!isdigit(serial[i])) {
            return false;
        }
    }
    return true;
}

static bool pem_is_valid(const char *pem)
{
    return strstr(pem, "-----BEGIN RSA PRIVATE KEY-----") != NULL &&
           strstr(pem, "-----END RSA PRIVATE KEY-----") != NULL;
}

void ps4key_clear_data(ps4key_data_t *data)
{
    if (data == NULL) {
        return;
    }
    memset(data, 0, sizeof(*data));
}

bool ps4key_parse_text(const char *text, ps4key_data_t *data, const char **error)
{
    static uint8_t decoded[sizeof(ps4key_wire_header_t) +
                           PS4KEY_SERIAL_LENGTH +
                           PS4KEY_PEM_MAX_LENGTH +
                           PS4KEY_SIG_MAX_LENGTH];

    if ((text == NULL) || (data == NULL)) {
        if (error) {
            *error = "Missing input.";
        }
        return false;
    }

    if (strncmp(text, PS4KEY_TEXT_PREFIX, strlen(PS4KEY_TEXT_PREFIX)) != 0) {
        if (error) {
            *error = "Missing PS4K1 prefix.";
        }
        return false;
    }

    size_t decoded_len = 0;
    if (!base64_decode(text + strlen(PS4KEY_TEXT_PREFIX), decoded,
                       sizeof(decoded), &decoded_len)) {
        if (error) {
            *error = "Base64 decode failed.";
        }
        return false;
    }

    if (decoded_len < sizeof(ps4key_wire_header_t)) {
        if (error) {
            *error = "Serialized data is too short.";
        }
        return false;
    }

    const ps4key_wire_header_t *header = (const ps4key_wire_header_t *)decoded;
    if ((memcmp(header->magic, "PS4K", 4) != 0) ||
        (header->version != PS4KEY_STORAGE_VERSION)) {
        if (error) {
            *error = "Unexpected serialization header.";
        }
        return false;
    }

    size_t payload_len = (size_t)header->serial_len + header->pem_len + header->sig_len;
    if (decoded_len != sizeof(*header) + payload_len) {
        if (error) {
            *error = "Serialized length mismatch.";
        }
        return false;
    }

    if ((header->serial_len != PS4KEY_SERIAL_LENGTH) ||
        (header->pem_len == 0) || (header->pem_len > PS4KEY_PEM_MAX_LENGTH) ||
        (header->sig_len == 0) || (header->sig_len > PS4KEY_SIG_MAX_LENGTH)) {
        if (error) {
            *error = "Serialized part lengths are invalid.";
        }
        return false;
    }

    const uint8_t *payload = decoded + sizeof(*header);
    if (crc32_calc(payload, payload_len) != header->crc32) {
        if (error) {
            *error = "CRC32 mismatch.";
        }
        return false;
    }

    if (!serial_is_valid(payload, header->serial_len)) {
        if (error) {
            *error = "Serial must be exactly 16 decimal digits.";
        }
        return false;
    }

    const char *pem = (const char *)(payload + header->serial_len);
    const uint8_t *sig = payload + header->serial_len + header->pem_len;

    ps4key_clear_data(data);
    data->magic = PS4KEY_STORAGE_MAGIC;
    data->version = PS4KEY_STORAGE_VERSION;
    data->serial_len = header->serial_len;
    data->pem_len = header->pem_len;
    data->sig_len = header->sig_len;
    data->crc32 = header->crc32;
    memcpy(data->serial, payload, header->serial_len);
    memcpy(data->pem, pem, header->pem_len);
    memcpy(data->sig, sig, header->sig_len);
    data->serial[header->serial_len] = '\0';
    data->pem[header->pem_len] = '\0';

    if (!pem_is_valid(data->pem)) {
        if (error) {
            *error = "PEM content does not look like an RSA private key.";
        }
        ps4key_clear_data(data);
        return false;
    }

    return true;
}