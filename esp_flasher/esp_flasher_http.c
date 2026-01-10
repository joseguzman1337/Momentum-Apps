#include "esp_flasher_http.h"

#include <furi/core/log.h>
#include <storage/storage.h>
// Avoid RX_BUF_SIZE macro conflict between esp_flasher_uart and FlipperHTTP
#ifdef RX_BUF_SIZE
#undef RX_BUF_SIZE
#endif
#include "../flip_wifi/flipper_http/flipper_http.h"


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
    FlipperHTTP* fhttp,
    const char* url,
    const char* dest_path,
    const char* what) {
    if(!fhttp || !url || !dest_path) return false;

    FURI_LOG_I(TAG, "Downloading %s from %s", what, url);

    // Configure destination file path for FlipperHTTP (bytes mode)
    snprintf(fhttp->file_path, sizeof(fhttp->file_path), "%s", dest_path);
    fhttp->save_received_data = false;
    fhttp->is_bytes_request = true;
    fhttp->state = IDLE;

    if(!flipper_http_request(
           fhttp,
           BYTES,
           url,
           "{\"Content-Type\": \"application/octet-stream\"}",
           NULL)) {
        FURI_LOG_E(TAG, "Failed to start HTTP request for %s", what);
        return false;
    }

    // Block until finished or error
    fhttp->state = RECEIVING;
    while(fhttp->state == RECEIVING) {
        furi_delay_ms(100);
    }

    if(fhttp->state == ISSUE) {
        FURI_LOG_E(TAG, "HTTP error while downloading %s", what);
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

    FlipperHTTP* fhttp = flipper_http_alloc();
    if(!fhttp) {
        FURI_LOG_E(TAG, "Failed to alloc FlipperHTTP context");
        return false;
    }

    bool ok = true;

    char url[256];

    // Bootloader
    snprintf(url, sizeof(url), "%s/%s", MARAUDER_RELEASE_BASE, MARAUDER_BOOT_S2);
    ok = ok && esp_flasher_http_download_one(fhttp, url, boot_path, "bootloader");

    // Partitions
    if(ok) {
        snprintf(url, sizeof(url), "%s/%s", MARAUDER_RELEASE_BASE, MARAUDER_PART_S2);
        ok = ok && esp_flasher_http_download_one(fhttp, url, part_path, "partitions");
    }

    // boot_app0
    if(ok) {
        snprintf(url, sizeof(url), "%s/%s", MARAUDER_RELEASE_BASE, MARAUDER_BOOTAPP0);
        ok = ok && esp_flasher_http_download_one(fhttp, url, app0_path, "boot_app0");
    }

    // Firmware A
    if(ok) {
        snprintf(url, sizeof(url), "%s/%s", MARAUDER_RELEASE_BASE, MARAUDER_FIRM_S2);
        ok = ok && esp_flasher_http_download_one(fhttp, url, firm_path, "firmware A");
    }

    flipper_http_free(fhttp);

    if(!ok) {
        FURI_LOG_E(TAG, "Failed to download one or more Marauder components");
    }

    return ok;
}
