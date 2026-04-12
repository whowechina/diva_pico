#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#include <stdbool.h>

#include "common/tusb_common.h"
#include "device/usbd.h"

enum {
    REPORT_ID_JOYSTICK = 1,
    REPORT_ID_PS4_OUTPUT = 5,
    REPORT_ID_LED_SLIDER_1 = 4,
    REPORT_ID_LED_SLIDER_2 = 5,
    REPORT_ID_LED_BUTTON = 6,
    REPORT_ID_LED_COMPRESSED = 11,
};

#define DIVAPICO_PS4_VENDOR_ID 0x1532
#define DIVAPICO_PS4_PRODUCT_ID 0x0401

// because they are missing from tusb_hid.h
#define HID_STRING_INDEX(x) HID_REPORT_ITEM(x, 7, RI_TYPE_LOCAL, 1)
#define HID_STRING_INDEX_N(x, n) HID_REPORT_ITEM(x, 7, RI_TYPE_LOCAL, n)
#define HID_STRING_MINIMUM(x) HID_REPORT_ITEM(x, 8, RI_TYPE_LOCAL, 1)
#define HID_STRING_MINIMUM_N(x, n) HID_REPORT_ITEM(x, 8, RI_TYPE_LOCAL, n)
#define HID_STRING_MAXIMUM(x) HID_REPORT_ITEM(x, 9, RI_TYPE_LOCAL, 1)
#define HID_STRING_MAXIMUM_N(x, n) HID_REPORT_ITEM(x, 9, RI_TYPE_LOCAL, n)

// Joystick Report Descriptor Template - Based off Drewol/rp2040-gamecon
// Button Map | X | Y
//HID_REPORT_ID(REPORT_ID_JOYSTICK)

#define DIVAPICO_REPORT_DESC_JOYSTICK                                          \
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),                                    \
    HID_USAGE(HID_USAGE_DESKTOP_GAMEPAD),                                     \
    HID_COLLECTION(HID_COLLECTION_APPLICATION),                                \
        HID_LOGICAL_MIN(0), HID_LOGICAL_MAX(1),                                \
        HID_PHYSICAL_MIN(0), HID_PHYSICAL_MAX(1),                              \
        HID_REPORT_SIZE(1), HID_REPORT_COUNT(16),                              \
        HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),                                 \
        HID_USAGE_MIN(1), HID_USAGE_MAX(16),                                   \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),                     \
                                                                               \
        HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),                                \
        HID_LOGICAL_MAX(7),                                                    \
        HID_PHYSICAL_MAX_N(315, 2),                                            \
        HID_REPORT_SIZE(4), HID_REPORT_COUNT(1),                               \
        0x65, 0x14, /* Unit */                                                 \
        HID_USAGE(HID_USAGE_DESKTOP_HAT_SWITCH),                               \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_NO_NULL_POSITION),\
        0x65, 0x00, /* Unit None */                                            \
        HID_REPORT_COUNT(1),                                                   \
        HID_INPUT(HID_CONSTANT | HID_ARRAY | HID_ABSOLUTE),                 \
                                                                               \
        HID_LOGICAL_MAX_N(0xff, 2), HID_PHYSICAL_MAX_N(0xff, 2), /* Analog */  \
        HID_USAGE(HID_USAGE_DESKTOP_X), HID_USAGE(HID_USAGE_DESKTOP_Y),        \
        HID_USAGE(HID_USAGE_DESKTOP_Z), HID_USAGE(HID_USAGE_DESKTOP_RZ),       \
        HID_REPORT_SIZE(8), HID_REPORT_COUNT(4),                               \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),                     \
                                                                               \
        HID_USAGE_PAGE_N(HID_USAGE_PAGE_VENDOR, 2),                            \
        HID_USAGE(0x20),                                                       \
        HID_REPORT_COUNT(1),                                                   \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),                     \
                                                                               \
        HID_USAGE_N(0x2621, 2),                                                \
        HID_REPORT_COUNT(8),                                                   \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),                    \
    HID_COLLECTION_END

#define DIVAPICO_REPORT_DESC_PS4                                                \
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,                                        \
    0x85, 0x01,                                                                 \
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,                            \
    0x15, 0x00, 0x26, 0xFF, 0x00,                                               \
    0x75, 0x08, 0x95, 0x04, 0x81, 0x02,                                         \
    0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B, 0x01,          \
    0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,                             \
    0x65, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0E,                             \
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0E, 0x81, 0x02,                 \
    0x06, 0x00, 0xFF, 0x09, 0x20, 0x75, 0x06, 0x95, 0x01, 0x81, 0x02,           \
    0x05, 0x01, 0x09, 0x33, 0x09, 0x34,                                         \
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,           \
    0x06, 0x00, 0xFF, 0x09, 0x21, 0x95, 0x36, 0x81, 0x02,                       \
    0x85, 0x05, 0x09, 0x22, 0x95, 0x1F, 0x91, 0x02,                             \
    0x85, 0x03, 0x0A, 0x21, 0x27, 0x95, 0x2F, 0xB1, 0x02,                       \
    0x85, 0x02, 0x09, 0x24, 0x95, 0x24, 0xB1, 0x02,                             \
    0x85, 0x08, 0x09, 0x25, 0x95, 0x03, 0xB1, 0x02,                             \
    0x85, 0x10, 0x09, 0x26, 0x95, 0x04, 0xB1, 0x02,                             \
    0x85, 0x11, 0x09, 0x27, 0x95, 0x02, 0xB1, 0x02,                             \
    0x85, 0x12, 0x06, 0x02, 0xFF, 0x09, 0x21, 0x95, 0x0F, 0xB1, 0x02,           \
    0x85, 0x13, 0x09, 0x22, 0x95, 0x16, 0xB1, 0x02,                             \
    0x85, 0x14, 0x06, 0x05, 0xFF, 0x09, 0x20, 0x95, 0x10, 0xB1, 0x02,           \
    0x85, 0x15, 0x09, 0x21, 0x95, 0x2C, 0xB1, 0x02,                             \
    0x06, 0x80, 0xFF,                                                            \
    0x85, 0x80, 0x09, 0x20, 0x95, 0x06, 0xB1, 0x02,                             \
    0x85, 0x81, 0x09, 0x21, 0x95, 0x06, 0xB1, 0x02,                             \
    0x85, 0x82, 0x09, 0x22, 0x95, 0x05, 0xB1, 0x02,                             \
    0x85, 0x83, 0x09, 0x23, 0x95, 0x01, 0xB1, 0x02,                             \
    0x85, 0x84, 0x09, 0x24, 0x95, 0x04, 0xB1, 0x02,                             \
    0x85, 0x85, 0x09, 0x25, 0x95, 0x06, 0xB1, 0x02,                             \
    0x85, 0x86, 0x09, 0x26, 0x95, 0x06, 0xB1, 0x02,                             \
    0x85, 0x87, 0x09, 0x27, 0x95, 0x23, 0xB1, 0x02,                             \
    0x85, 0x88, 0x09, 0x28, 0x95, 0x22, 0xB1, 0x02,                             \
    0x85, 0x89, 0x09, 0x29, 0x95, 0x02, 0xB1, 0x02,                             \
    0x85, 0x90, 0x09, 0x30, 0x95, 0x05, 0xB1, 0x02,                             \
    0x85, 0x91, 0x09, 0x31, 0x95, 0x03, 0xB1, 0x02,                             \
    0x85, 0x92, 0x09, 0x32, 0x95, 0x03, 0xB1, 0x02,                             \
    0x85, 0x93, 0x09, 0x33, 0x95, 0x0C, 0xB1, 0x02,                             \
    0x85, 0xA0, 0x09, 0x40, 0x95, 0x06, 0xB1, 0x02,                             \
    0x85, 0xA1, 0x09, 0x41, 0x95, 0x01, 0xB1, 0x02,                             \
    0x85, 0xA2, 0x09, 0x42, 0x95, 0x01, 0xB1, 0x02,                             \
    0x85, 0xA3, 0x09, 0x43, 0x95, 0x30, 0xB1, 0x02,                             \
    0x85, 0xA4, 0x09, 0x44, 0x95, 0x0D, 0xB1, 0x02,                             \
    0x85, 0xA5, 0x09, 0x45, 0x95, 0x15, 0xB1, 0x02,                             \
    0x85, 0xA6, 0x09, 0x46, 0x95, 0x15, 0xB1, 0x02,                             \
    0x85, 0xA7, 0x09, 0x4A, 0x95, 0x01, 0xB1, 0x02,                             \
    0x85, 0xA8, 0x09, 0x4B, 0x95, 0x01, 0xB1, 0x02,                             \
    0x85, 0xA9, 0x09, 0x4C, 0x95, 0x08, 0xB1, 0x02,                             \
    0x85, 0xAA, 0x09, 0x4E, 0x95, 0x01, 0xB1, 0x02,                             \
    0x85, 0xAB, 0x09, 0x4F, 0x95, 0x39, 0xB1, 0x02,                             \
    0x85, 0xAC, 0x09, 0x50, 0x95, 0x39, 0xB1, 0x02,                             \
    0x85, 0xAD, 0x09, 0x51, 0x95, 0x0B, 0xB1, 0x02,                             \
    0x85, 0xAE, 0x09, 0x52, 0x95, 0x01, 0xB1, 0x02,                             \
    0x85, 0xAF, 0x09, 0x53, 0x95, 0x02, 0xB1, 0x02,                             \
    0x85, 0xB0, 0x09, 0x54, 0x95, 0x3F, 0xB1, 0x02,                             \
    0xC0,                                                                       \
    0x06, 0xF0, 0xFF, 0x09, 0x40, 0xA1, 0x01,                                   \
    0x85, 0xF0, 0x09, 0x47, 0x95, 0x3F, 0xB1, 0x02,                             \
    0x85, 0xF1, 0x09, 0x48, 0x95, 0x3F, 0xB1, 0x02,                             \
    0x85, 0xF2, 0x09, 0x49, 0x95, 0x0F, 0xB1, 0x02,                             \
    0x85, 0xF3, 0x0A, 0x01, 0x47, 0x95, 0x07, 0xB1, 0x02,                       \
    0xC0

#define DIVAPICO_LED_HEADER \
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP), HID_USAGE(0x00),                   \
    HID_COLLECTION(HID_COLLECTION_APPLICATION),                                \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(8),                               \
        HID_INPUT(HID_CONSTANT | HID_VARIABLE | HID_ABSOLUTE)

#define DIVAPICO_LED_FOOTER \
    HID_COLLECTION_END

// Slider First 16 LEDs (48 rgb zones, BRG order)
#define DIVAPICO_REPORT_DESC_LED_SLIDER_1                                      \
        HID_REPORT_ID(REPORT_ID_LED_SLIDER_1)                                  \
        HID_REPORT_COUNT(48), HID_REPORT_SIZE(8),                              \
        HID_LOGICAL_MIN(0x00), HID_LOGICAL_MAX_N(0x00ff, 2),                   \
        HID_USAGE_PAGE(HID_USAGE_PAGE_ORDINAL),                                \
        HID_USAGE_MIN(1), HID_USAGE_MAX(48),                                   \
        HID_STRING_MINIMUM(8), HID_STRING_MAXIMUM(55),                         \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE)

// Slider Remaining 16 LEDs (48 rgb zones, BRG order)
#define DIVAPICO_REPORT_DESC_LED_SLIDER_2                                      \
        HID_REPORT_ID(REPORT_ID_LED_SLIDER_2)                                  \
        HID_REPORT_COUNT(48), HID_REPORT_SIZE(8),                              \
        HID_LOGICAL_MIN(0x00), HID_LOGICAL_MAX_N(0x00ff, 2),                   \
        HID_USAGE_PAGE(HID_USAGE_PAGE_ORDINAL),                                \
        HID_USAGE_MIN(49), HID_USAGE_MAX(96),                                  \
        HID_STRING_MINIMUM(56), HID_STRING_MAXIMUM(103), /* Delta to previous */ \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE)

// Button LEDs (18 rgb zones, BRG order)
#define DIVAPICO_REPORT_DESC_LED_BUTTON                                        \
        HID_REPORT_ID(REPORT_ID_LED_BUTTON)                                    \
        HID_REPORT_COUNT(4), HID_REPORT_SIZE(8),                               \
        HID_LOGICAL_MIN(0x00), HID_LOGICAL_MAX_N(0x00ff, 2),                   \
        HID_USAGE_PAGE(HID_USAGE_PAGE_ORDINAL),                                \
        HID_USAGE_MIN(97), HID_USAGE_MAX(100),                                 \
        HID_STRING_MINIMUM(104), HID_STRING_MAXIMUM(107), /* Delta to previous */ \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE)

// LEDs Compressed
#define DIVAPICO_REPORT_DESC_LED_COMPRESSED                                     \
        HID_REPORT_ID(REPORT_ID_LED_COMPRESSED)                                \
        HID_USAGE_PAGE(HID_USAGE_PAGE_ORDINAL),                                \
        HID_USAGE(0x00),                                                      \
        HID_LOGICAL_MIN(0x00), HID_LOGICAL_MAX_N(0x00ff, 2),                   \
        HID_REPORT_SIZE(8), HID_REPORT_COUNT(63),                              \
        HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE)

void hid_use_ps4(bool enable);

#endif /* USB_DESCRIPTORS_H_ */
