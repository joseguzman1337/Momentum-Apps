#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_eth.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FuriHalUsbInterface* previous_usb;
    enum {
        UsbEthernetPingNotRun,
        UsbEthernetPingSuccess,
        UsbEthernetPingFailed,
    } ping_status;
} UsbEthernetApp;

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

static int32_t usb_ethernet_run_headless(char* args) {
    char* cursor = args;
    char* command = usb_ethernet_next_arg(&cursor);
    char* first = usb_ethernet_next_arg(&cursor);
    char* second = usb_ethernet_next_arg(&cursor);
    char* third = usb_ethernet_next_arg(&cursor);

    if(!command) return -1;

    FuriHalUsbInterface* previous_usb = furi_hal_usb_get_config();
    if(furi_hal_usb_is_locked()) furi_hal_usb_unlock();
    if(!furi_hal_usb_set_config(&usb_eth, NULL)) return -1;

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

    furi_hal_usb_set_config(previous_usb, NULL);
    return success ? 0 : -1;
}

int32_t usb_ethernet_app(void* context) {
    if(context && *(const char*)context) {
        char* args = strdup(context);
        if(!args) return -1;
        int32_t result = usb_ethernet_run_headless(args);
        free(args);
        return result;
    }

    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    UsbEthernetApp app = {.previous_usb = furi_hal_usb_get_config()};

    if(furi_hal_usb_is_locked()) furi_hal_usb_unlock();
    if(!furi_hal_usb_set_config(&usb_eth, NULL)) {
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
            app.ping_status = furi_hal_usb_eth_ping("172.16.0.1", 2, 1500) ?
                                  UsbEthernetPingSuccess :
                                  UsbEthernetPingFailed;
            view_port_update(viewport);
        }
    }

    gui_remove_view_port(gui, viewport);
    view_port_free(viewport);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(queue);
    furi_hal_usb_set_config(app.previous_usb, NULL);
    return 0;
}
