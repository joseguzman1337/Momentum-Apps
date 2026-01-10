#include "esp_flasher_http.h"

#include <furi/core/log.h>
#include <storage/storage.h>
#include <furi_hal_usb_eth.h>

#define TAG "EspFlasherHTTP"

// GitHub "latest" release asset base for ESP32Marauder
// NOTE: This assumes the Marauder project continues to publish the same
// asset filenames for the Flipper Wi-Fi Devboard target.
#define MARAUDER_RELEASE_BASE "https://github.com/justcallmekoko/ESP32Marauder/releases/latest/download"

// Concrete asset names for Wi-Fi Devboard (S2) build
#define MARAUDER_BOOT_S2   "esp32_marauder.ino.bootloader.bin"
#define MARAUDER_PART_S2   "esp32_marauder.ino.partitions.bin"
#define MARAUDER_BOOTAPP0  "boot_app0.bin"
#define MARAUDER_FIRM_S2   "esp32_marauder.flipper.bin"

static bool esp_flasher_http_mkdir_rec(Storage* storage, const char* path) {
    if(!storage) return false;
    FURI_LOG_I(TAG, "mkdir %s", path);
    storage_common_mkdir(storage, path);
    return true;
}

static bool esp_flasher_http_prepare_dirs(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        FURI_LOG_E(TAG, "Failed to open storage record");
        return false;
    }

    char dir[256];

    // apps_data/esp_flasher
    snprintf(dir, sizeof(dir), "%s", ESP_APP_FOLDER);
    esp_flasher_http_mkdir_rec(storage, dir);

    // apps_data/esp_flasher/assets
    snprintf(dir, sizeof(dir), "%s/assets", ESP_APP_FOLDER);
    esp_flasher_http_mkdir_rec(storage, dir);

    // apps_data/esp_flasher/assets/marauder
    snprintf(dir, sizeof(dir), "%s/assets/marauder", ESP_APP_FOLDER);
    esp_flasher_http_mkdir_rec(storage, dir);

    // apps_data/esp_flasher/assets/marauder/s2
    snprintf(dir, sizeof(dir), "%s/assets/marauder/s2", ESP_APP_FOLDER);
    esp_flasher_http_mkdir_rec(storage, dir);

    furi_record_close(RECORD_STORAGE);
    return true;
}

static bool esp_flasher_http_download_one(
    const char* url,
    const char* dest_path,
    const char* what) {
    if(!url || !dest_path) return false;

    FURI_LOG_I(TAG, "Downloading %s from %s", what, url);

    /* Perform blocking HTTP GET over usb_eth/lwIP into dest_path. */
    if(!furi_hal_usb_eth_http_download_to_file(url, dest_path, 60000)) {
        FURI_LOG_E(TAG, "Failed to HTTP-download %s", what);
        return false;
    }

    // Basic sanity: ensure file exists and nonzero size
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        FURI_LOG_E(TAG, "Failed to reopen storage");
        return false;
    }

    bool ok = false;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, dest_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t sz = storage_file_size(file);
        FURI_LOG_I(TAG, "%s size=%llu", what, sz);
        ok = (sz > 0);
        storage_file_close(file);
    } else {
        FURI_LOG_E(TAG, "File %s not found after download", dest_path);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    if(!ok) {
        FURI_LOG_E(TAG, "Downloaded %s has invalid size", what);
    }

    return ok;
}

bool esp_flasher_http_download_marauder_flipper_s2(
    EspFlasherApp* app,
    const char* boot_path,
    const char* part_path,
    const char* app0_path,
    const char* firm_path) {
    if(!app) return false;

    if(!esp_flasher_http_prepare_dirs()) {
        return false;
    }

    bool ok = true;

    char url[256];

    // Bootloader
    snprintf(url, sizeof(url), "%s/%s", MARAUDER_RELEASE_BASE, MARAUDER_BOOT_S2);
    ok = ok && esp_flasher_http_download_one(url, boot_path, "bootloader");

    // Partitions
    if(ok) {
        snprintf(url, sizeof(url), "%s/%s", MARAUDER_RELEASE_BASE, MARAUDER_PART_S2);
        ok = ok && esp_flasher_http_download_one(url, part_path, "partitions");
    }

    // boot_app0
    if(ok) {
        snprintf(url, sizeof(url), "%s/%s", MARAUDER_RELEASE_BASE, MARAUDER_BOOTAPP0);
        ok = ok && esp_flasher_http_download_one(url, app0_path, "boot_app0");
    }

    // Firmware A
    if(ok) {
        snprintf(url, sizeof(url), "%s/%s", MARAUDER_RELEASE_BASE, MARAUDER_FIRM_S2);
        ok = ok && esp_flasher_http_download_one(url, firm_path, "firmware A");
    }

    if(!ok) {
        FURI_LOG_E(TAG, "Failed to download one or more Marauder components");
    }

    return ok;
}
