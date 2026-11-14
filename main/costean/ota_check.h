#pragma once
/* Costean OS: check an uploaded file before it is written to flash, so a wrong file is refused
 * with a clear message instead of "succeeding" and leaving the old firmware running.
 * Pure C (no ESP-IDF), host tested in test/costean/test_ota_check.c. */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes needed from the start of the file: image header + first segment header + app description. */
#define COSTEAN_OTA_HEAD 288

/* Firmware (/api/system/OTA): an ESP32-S3 app image of the "esp-miner" project that fits the slot.
 * head = the first min(n, COSTEAN_OTA_HEAD) bytes, total = Content-Length, slot = OTA partition size.
 * Returns NULL if ok (and copies the version found in the file), else a message for the user. */
const char *costean_check_app_image(const uint8_t *head, size_t n, size_t total, size_t slot,
                                    char *version, size_t version_len);

/* Web UI (/api/system/OTAWWW): anything but a firmware / factory image, not empty, fits the partition. */
const char *costean_check_www_image(const uint8_t *head, size_t n, size_t total, size_t part);

#ifdef __cplusplus
}
#endif
