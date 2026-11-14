#include "ota_check.h"

#include <stdio.h>
#include <string.h>

/* esp_image_header_t (24 bytes): magic 0xE9 at 0, chip_id (uint16) at 12.
 * esp_image_segment_header_t (8 bytes) follows, then the first segment starts with
 * esp_app_desc_t: magic 0xABCD5432 (u32), secure_version, 2 reserved words, version[32] at +16,
 * project_name[32] at +48. */
#define IMG_MAGIC 0xE9
#define CHIP_ID_ESP32S3 9
#define DESC_OFS 32
#define DESC_MAGIC 0xABCD5432u

static uint32_t le32(const uint8_t *p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }

const char *costean_check_app_image(const uint8_t *h, size_t n, size_t total, size_t slot, char *version, size_t vlen)
{
    if (version && vlen) version[0] = 0;
    if (total == 0) return "The upload was empty. Pick esp-miner.bin and try again.";
    if (n < COSTEAN_OTA_HEAD || total < 65536) return "That file is too small to be firmware. Pick esp-miner.bin.";
    if (h[0] != IMG_MAGIC)
        return "That is not a firmware file. Upload esp-miner.bin here (www.bin goes in the web UI box).";
    if (total > slot)
        return "That file is bigger than the firmware slot. It is probably the factory image: flash factory images at 0x0 over USB, and upload esp-miner.bin here.";
    if ((h[12] | (h[13] << 8)) != CHIP_ID_ESP32S3) return "That firmware is for a different chip (not ESP32-S3).";
    if (le32(h + DESC_OFS) != DESC_MAGIC)
        return "That file is a bootloader or factory image, not the app. Upload esp-miner.bin.";
    char proj[33];
    memcpy(proj, h + DESC_OFS + 48, 32); proj[32] = 0;
    if (strcmp(proj, "esp-miner") != 0) return "That firmware is not ESP-Miner / Costean OS.";
    if (version && vlen) {
        char v[33];
        memcpy(v, h + DESC_OFS + 16, 32); v[32] = 0;
        snprintf(version, vlen, "%s", v);
    }
    return NULL;
}

const char *costean_check_www_image(const uint8_t *h, size_t n, size_t total, size_t part)
{
    if (total == 0) return "The upload was empty. Pick www.bin and try again.";
    if (n >= 1 && h[0] == IMG_MAGIC)
        return "That is a firmware file (esp-miner.bin). Upload it in the Firmware box, not as www.bin. Nothing was changed.";
    if (total < 4096) return "That file is too small to be www.bin.";
    if (total > part) return "That file is bigger than the web UI partition (3 MB). Is it the factory image? Nothing was changed.";
    return NULL;
}
