#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t mouse_hid_report_descriptor[];
extern const uint8_t mouse_hid_configuration_descriptor[];
extern const char* mouse_hid_string_descriptor[];
extern const uint8_t mouse_hid_string_descriptor_count;

bool mouse_hid_boot_protocol(void);

#ifdef __cplusplus
}
#endif
