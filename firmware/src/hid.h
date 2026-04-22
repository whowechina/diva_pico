#ifndef HID_H_
#define HID_H_

#include <stdbool.h>
#include <stdint.h>

void hid_update(uint16_t buttons, uint32_t touch);
bool hid_shift_activated(void);
void hid_apply_mode(void);

#endif
