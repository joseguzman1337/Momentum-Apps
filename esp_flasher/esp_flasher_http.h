#pragma once

#include "esp_flasher_app_i.h"

/**
 * Download the latest ESP32 Marauder firmware for the Flipper Wi-Fi Devboard (S2)
 * into the ESP Flasher app data folder using FlipperHTTP.
 *
 * This will download (over Wi-Fi via FlipperHTTP):
 *  - bootloader  -> ESP_ADDR_BOOT (0x1000 for S2)
 *  - partitions  -> ESP_ADDR_PART (0x8000)
 *  - boot_app0   -> ESP_ADDR_BOOT_APP0 (0xE000)
 *  - firmware A  -> ESP_ADDR_APP_A (0x10000)
 *
 * Files are stored under:
 *  ESP_APP_FOLDER/assets/marauder/s2/
 * and the generic marauder assets folder.
 *
 * On success, the files at the provided paths will exist and be ready for flashing.
 */
bool esp_flasher_http_download_marauder_flipper_s2(
    EspFlasherApp* app,
    const char* boot_path,
    const char* part_path,
    const char* app0_path,
    const char* firm_path);
