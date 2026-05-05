#pragma once
#include <stddef.h>
#include <stdint.h>

void parse_ssid(const uint8_t *tagged, size_t len, char *out, size_t out_size);

// BLE AD list parser. Pulls Local Name (0x08/0x09) and mfg-id from 0xFF.
// out_name buffer must be at least WLOG_NAME_MAX bytes. mfg_id may be NULL.
void parse_adv_data(const uint8_t *adv, size_t len,
                    char *out_name, size_t out_size, uint16_t *mfg_id);
