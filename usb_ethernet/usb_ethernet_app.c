#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_eth.h>
#include <gui/gui.h>
#include <input/input.h>

typedef struct {
    FuriMessageQueue* queue;
    FuriHalUsbInterface* previous_usb;
    bool ping_ok;
    bool ping_attempted;
} UsbEthernetApp;

static void usb_ethernet_draw(Canvas* canvas, void* context) {
    UsbEthernetApp* app = context;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 8, 12, "USB Ethernet");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 8, 28, "CDC-ECM active");
    canvas_draw_str(canvas, 8, 40, "OK: ping gateway");
    if(app->ping_attempted) {
        canvas_draw_str(canvas, 8, 52, app->ping_ok ? "Ping: success" : "Ping: failed");
    }
    canvas_draw_str(canvas, 8, 63, "Back: restore USB");
}

static void usb_ethernet_input(InputEvent* event, void* context) {
    UsbEthernetApp* app = context;
    furi_message_queue_put(app->queue, event, FuriWaitForever);
}

int32_t usb_ethernet_app(void* context) {
    UNUSED(context);

    UsbEthernetApp app = {
        .queue = furi_message_queue_alloc(8, sizeof(InputEvent)),
        .previous_usb = furi_hal_usb_get_config(),
    };

    if(furi_hal_usb_is_locked()) furi_hal_usb_unlock();
    if(!furi_hal_usb_set_config(&usb_eth, NULL)) {
        furi_message_queue_free(app.queue);
        return -1;
    }

    ViewPort* viewport = view_port_alloc();
    view_port_draw_callback_set(viewport, usb_ethernet_draw, &app);
    view_port_input_callback_set(viewport, usb_ethernet_input, &app);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, viewport, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app.queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        if((event.type == InputTypeShort) && (event.key == InputKeyBack)) {
            running = false;
        } else if((event.type == InputTypeShort) && (event.key == InputKeyOk)) {
            app.ping_attempted = true;
            app.ping_ok = furi_hal_usb_eth_ping("172.16.0.1", 2, 1500);
            view_port_update(viewport);
        }
    }

    gui_remove_view_port(gui, viewport);
    view_port_free(viewport);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app.queue);
    furi_hal_usb_set_config(app.previous_usb, NULL);
    return 0;
}
