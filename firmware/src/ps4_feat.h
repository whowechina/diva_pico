#ifndef PS4_FEAT_H
#define PS4_FEAT_H

#include <stdint.h>

#include "tusb.h"

uint16_t ps4_feat_get_report(uint8_t report_id, hid_report_type_t report_type,
                             uint8_t *buffer, uint16_t reqlen);

void ps4_feat_set_report(uint8_t report_id, hid_report_type_t report_type,
                         uint8_t const *buffer, uint16_t bufsize);

#endif
