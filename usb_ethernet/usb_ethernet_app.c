#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_eth.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <string.h>
#include "usb_ethernet_service.h"

typedef struct {
    FuriHalUsbInterface* previous_usb;
    bool was_locked;
    bool usb_active;
    bool record_active;
    FuriMutex* service_mutex;
    UsbEthernetService service;
    enum {
        UsbEthernetPingNotRun,
        UsbEthernetPingSuccess,
        UsbEthernetPingFailed,
    } ping_status;
} UsbEthernetApp;

static bool usb_ethernet_service_ping(
    void* context,
    const char* host,
    uint32_t count,
    uint32_t timeout_ms) {
    UsbEthernetApp* app = context;
    furi_mutex_acquire(app->service_mutex, FuriWaitForever);
    bool result = app->usb_active && furi_hal_usb_eth_ping(host, count, timeout_ms);
    furi_mutex_release(app->service_mutex);
    return result;
}

static bool usb_ethernet_service_http(
    void* context,
    const char* url,
    const char* dest_path,
    uint32_t timeout_ms) {
    UsbEthernetApp* app = context;
    furi_mutex_acquire(app->service_mutex, FuriWaitForever);
    bool result = app->usb_active &&
                  furi_hal_usb_eth_http_download_to_file(url, dest_path, timeout_ms);
    furi_mutex_release(app->service_mutex);
    return result;
}

static bool usb_ethernet_service_status(void* context) {
    UsbEthernetApp* app = context;
    furi_mutex_acquire(app->service_mutex, FuriWaitForever);
    bool result = app->usb_active;
    furi_mutex_release(app->service_mutex);
    return result;
}

static bool usb_ethernet_activate(UsbEthernetApp* app, bool publish_service) {
    app->previous_usb = furi_hal_usb_get_config();
    app->was_locked = furi_hal_usb_is_locked();
    app->service_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->service = (UsbEthernetService){
        .context = app,
        .ping = usb_ethernet_service_ping,
        .http_download = usb_ethernet_service_http,
        .status = usb_ethernet_service_status,
    };

    if(app->was_locked) furi_hal_usb_unlock();
    if(!furi_hal_usb_set_config(&usb_eth, NULL)) {
        if(app->was_locked) furi_hal_usb_lock();
        furi_mutex_free(app->service_mutex);
        app->service_mutex = NULL;
        return false;
    }

    app->usb_active = true;
    if(publish_service) {
        furi_record_create(RECORD_USB_ETHERNET, &app->service);
        app->record_active = true;
    }
    return true;
}

static void usb_ethernet_deactivate(UsbEthernetApp* app) {
    furi_mutex_acquire(app->service_mutex, FuriWaitForever);
    app->usb_active = false;
    furi_mutex_release(app->service_mutex);

    if(app->record_active) {
        while(!furi_record_destroy(RECORD_USB_ETHERNET)) furi_delay_ms(10);
        app->record_active = false;
    }

    furi_hal_usb_set_config(app->previous_usb, NULL);
    if(app->was_locked) furi_hal_usb_lock();
    furi_mutex_free(app->service_mutex);
    app->service_mutex = NULL;
}

static void usb_ethernet_draw(Canvas* canvas, void* context) {
    UsbEthernetApp* app = context;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 8, 12, "USB Ethernet");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 8, 28, "CDC-ECM active");
    canvas_draw_str(canvas, 8, 40, "OK: ping gateway");
    if(app->ping_status != UsbEthernetPingNotRun) {
        canvas_draw_str(
            canvas,
            8,
            52,
            app->ping_status == UsbEthernetPingSuccess ? "Ping: success" : "Ping: failed");
    }
    canvas_draw_str(canvas, 8, 63, "Back: restore USB");
}

static void usb_ethernet_input(InputEvent* event, void* context) {
    furi_message_queue_put(context, event, FuriWaitForever);
}

static char* usb_ethernet_next_arg(char** cursor) {
    while(**cursor == ' ') (*cursor)++;
    if(**cursor == '\0') return NULL;

    char* arg = *cursor;
    while((**cursor != '\0') && (**cursor != ' ')) (*cursor)++;
    if(**cursor != '\0') *(*cursor)++ = '\0';
    return arg;
}

static uint32_t usb_ethernet_parse_u32(const char* value, uint32_t fallback) {
    if(!value) return fallback;
    char* end;
    unsigned long parsed = strtoul(value, &end, 10);
    return ((*value != '\0') && (*end == '\0') && (parsed <= UINT32_MAX)) ?
               (uint32_t)parsed :
               fallback;
}

static int32_t usb_ethernet_run_headless(UsbEthernetApp* app, char* args) {
    char* cursor = args;
    char* command = usb_ethernet_next_arg(&cursor);
    char* first = usb_ethernet_next_arg(&cursor);
    char* second = usb_ethernet_next_arg(&cursor);
    char* third = usb_ethernet_next_arg(&cursor);

    if(!command) return -1;

    if(!usb_ethernet_activate(app, false)) return -1;

    bool success = false;
    if(strcmp(command, "ping") == 0 && first) {
        success = furi_hal_usb_eth_ping(
            first,
            usb_ethernet_parse_u32(second, 2),
            usb_ethernet_parse_u32(third, 1500));
        printf(success ? "Ping success!\r\n" : "Ping failed.\r\n");
    } else if(strcmp(command, "http") == 0 && first && second) {
        success = furi_hal_usb_eth_http_download_to_file(
            first, second, usb_ethernet_parse_u32(third, 30000));
    }

    usb_ethernet_deactivate(app);
    return success ? 0 : -1;
}

int32_t usb_ethernet_app(void* context) {
    if(context && *(const char*)context) {
        UsbEthernetApp app = {0};
        char* args = strdup(context);
        if(!args) return -1;
        int32_t result = usb_ethernet_run_headless(&app, args);
        free(args);
        return result;
    }

    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    UsbEthernetApp app = {0};

    if(!usb_ethernet_activate(&app, true)) {
        furi_message_queue_free(queue);
        return -1;
    }

    ViewPort* viewport = view_port_alloc();
    view_port_draw_callback_set(viewport, usb_ethernet_draw, &app);
    view_port_input_callback_set(viewport, usb_ethernet_input, queue);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, viewport, GuiLayerFullscreen);

    InputEvent event;
    while(true) {
        if(furi_message_queue_get(queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        if((event.type == InputTypeShort) && (event.key == InputKeyBack)) {
            break;
        } else if((event.type == InputTypeShort) && (event.key == InputKeyOk)) {
            app.ping_status = usb_ethernet_service_ping(&app, "172.16.0.1", 2, 1500) ?
                                  UsbEthernetPingSuccess :
                                  UsbEthernetPingFailed;
            view_port_update(viewport);
        }
    }

    gui_remove_view_port(gui, viewport);
    view_port_free(viewport);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(queue);
    usb_ethernet_deactivate(&app);
    return 0;
}
