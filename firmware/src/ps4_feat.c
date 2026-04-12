#include <string.h>

#include "ps4_feat.h"

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

static const uint8_t ps4_feat_f2[] = {
    0x00, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t ps4_feat_f3[] = {
    0x00, 0x38, 0x38, 0x00, 0x00, 0x00, 0x00
};

static uint8_t ps4_feat_13[22];
static uint8_t ps4_feat_14[16];
static uint8_t ps4_feat_15[44];

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
        case 0xf2: return sizeof(ps4_feat_f2);
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
        case 0xf2: return ps4_feat_f2;
        case 0xf3: return ps4_feat_f3;
        default: return NULL;
    }
}

uint16_t ps4_feat_get_report(uint8_t report_id, hid_report_type_t report_type,
                             uint8_t *buffer, uint16_t reqlen)
{
    if (report_type != HID_REPORT_TYPE_FEATURE) {
        memset(buffer, 0, reqlen);
        return reqlen;
    }

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

void ps4_feat_set_report(uint8_t report_id, hid_report_type_t report_type,
                         uint8_t const *buffer, uint16_t bufsize)
{
    if (report_type != HID_REPORT_TYPE_FEATURE) {
        return;
    }

    uint8_t *dest = NULL;
    uint16_t dest_len = 0;

    if (report_id == 0x13) {
        dest = ps4_feat_13;
        dest_len = sizeof(ps4_feat_13);
    } else if (report_id == 0x14) {
        dest = ps4_feat_14;
        dest_len = sizeof(ps4_feat_14);
    } else if (report_id == 0x15) {
        dest = ps4_feat_15;
        dest_len = sizeof(ps4_feat_15);
    }

    if (dest == NULL) {
        return;
    }

    uint16_t copy_len = bufsize;
    if (copy_len > dest_len) {
        copy_len = dest_len;
    }

    memset(dest, 0, dest_len);
    memcpy(dest, buffer, copy_len);
}
