#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RECORD_USB_ETHERNET "usb_ethernet"

typedef struct UsbEthernetService UsbEthernetService;

struct UsbEthernetService {
    void* context;
    bool (*ping)(void* context, const char* host, uint32_t count, uint32_t timeout_ms);
    bool (*http_download)(
        void* context,
        const char* url,
        const char* dest_path,
        uint32_t timeout_ms);
    bool (*status)(void* context);
};
