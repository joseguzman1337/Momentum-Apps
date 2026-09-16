#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <furi_hal_usb.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FuriHalUsbEthConfig {
    uint16_t vid;
    uint16_t pid;
    char manuf[32];
    char product[32];
} FuriHalUsbEthConfig;

extern FuriHalUsbInterface usb_eth;

bool furi_hal_usb_eth_ping(const char* host, uint32_t count, uint32_t timeout_ms);

bool furi_hal_usb_eth_http_download_to_file(
    const char* url,
    const char* dest_path,
    uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
