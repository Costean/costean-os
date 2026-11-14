/* Host test: the OTA upload checks against the real build outputs (when present) and crafted files. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ota_check.h"

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define SLOT (4u * 1024 * 1024)
#define WWW (3u * 1024 * 1024)

static size_t load(const char *path, uint8_t *head, size_t *total)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(head, 1, COSTEAN_OTA_HEAD, f);
    fseek(f, 0, SEEK_END); *total = (size_t) ftell(f); fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    uint8_t h[COSTEAN_OTA_HEAD]; size_t total = 0, n; char ver[40];
    /* crafted app image */
    memset(h, 0, sizeof h); h[0] = 0xE9; h[12] = 9;
    h[32] = 0x32; h[33] = 0x54; h[34] = 0xCD; h[35] = 0xAB;
    strcpy((char *) h + 32 + 16, "1.2.0-costean.5"); strcpy((char *) h + 32 + 48, "esp-miner");
    CHECK(costean_check_app_image(h, sizeof h, 2200000, SLOT, ver, sizeof ver) == NULL && !strcmp(ver, "1.2.0-costean.5"));
    CHECK(costean_check_www_image(h, sizeof h, 2200000, WWW) != NULL);            /* app in the www box: refused */
    CHECK(costean_check_app_image(h, sizeof h, 15802368, SLOT, ver, sizeof ver) != NULL);   /* too big: factory */
    CHECK(costean_check_app_image(h, sizeof h, 0, SLOT, ver, sizeof ver) != NULL);
    CHECK(costean_check_app_image(h, 10, 2200000, SLOT, ver, sizeof ver) != NULL);
    h[12] = 0; CHECK(costean_check_app_image(h, sizeof h, 2200000, SLOT, ver, sizeof ver) != NULL); h[12] = 9;
    strcpy((char *) h + 32 + 48, "other"); CHECK(costean_check_app_image(h, sizeof h, 2200000, SLOT, ver, sizeof ver) != NULL);
    h[32] = 0; CHECK(costean_check_app_image(h, sizeof h, 2200000, SLOT, ver, sizeof ver) != NULL);   /* no app description */
    uint8_t spiffs[COSTEAN_OTA_HEAD]; memset(spiffs, 0xff, sizeof spiffs);
    CHECK(costean_check_app_image(spiffs, sizeof spiffs, WWW, SLOT, ver, sizeof ver) != NULL);   /* www in the firmware box */
    CHECK(costean_check_www_image(spiffs, sizeof spiffs, WWW, WWW) == NULL);
    CHECK(costean_check_www_image(spiffs, sizeof spiffs, WWW + 1, WWW) != NULL);
    /* real files: argv = esp-miner.bin www.bin factory.bin */
    if (argc > 3) {
        if ((n = load(argv[1], h, &total))) {
            CHECK(costean_check_app_image(h, n, total, SLOT, ver, sizeof ver) == NULL);
            CHECK(strncmp(ver, "1.2.0-costean.", 14) == 0);
            CHECK(costean_check_www_image(h, n, total, WWW) != NULL);
            printf("esp-miner.bin: firmware ok, version %s; refused as www.bin\n", ver);
        }
        if ((n = load(argv[2], h, &total))) {
            CHECK(costean_check_app_image(h, n, total, SLOT, ver, sizeof ver) != NULL);
            CHECK(costean_check_www_image(h, n, total, WWW) == NULL);
            printf("www.bin: refused as firmware; ok as www.bin\n");
        }
        if ((n = load(argv[3], h, &total))) {
            CHECK(costean_check_app_image(h, n, total, SLOT, ver, sizeof ver) != NULL);
            CHECK(costean_check_www_image(h, n, total, WWW) != NULL);
            printf("factory image: refused in both boxes\n");
        }
    }
    printf("ota_check: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
