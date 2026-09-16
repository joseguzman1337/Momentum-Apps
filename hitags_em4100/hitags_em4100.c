#include "hitags.h"

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>

#include <stdio.h>
#include <string.h>

#define TAG "HitagWriter"

typedef enum {
    HitagScreenEdit,
    HitagScreenWarning,
    HitagScreenReading,
    HitagScreenReady,
    HitagScreenWriteSent,
    HitagScreenError,
} HitagScreen;

typedef struct {
    FuriMutex* mutex;
    FuriMessageQueue* queue;
    HitagScreen screen;
    uint8_t em4100[5];
    uint8_t cursor;
    uint8_t uid[LFRFID_HITAGS_UID_SIZE];
    const char* error;
} HitagWriter;

static void hitag_input_callback(InputEvent* event, void* context) {
    furi_message_queue_put(context, event, 0);
}

static void hitag_draw_callback(Canvas* canvas, void* context) {
    HitagWriter* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Hitag S / ID8268");
    canvas_set_font(canvas, FontSecondary);

    char line[32];
    switch(app->screen) {
    case HitagScreenEdit:
        canvas_draw_str(canvas, 2, 23, "EM4100 ID (hex):");
        snprintf(
            line,
            sizeof(line),
            "%02X %02X %02X %02X %02X",
            app->em4100[0],
            app->em4100[1],
            app->em4100[2],
            app->em4100[3],
            app->em4100[4]);
        canvas_draw_str(canvas, 12, 39, line);
        canvas_draw_line(canvas, 12 + app->cursor * 18, 42, 24 + app->cursor * 18, 42);
        canvas_draw_str(canvas, 2, 62, "UP/DN value  OK continue");
        break;
    case HitagScreenWarning:
        canvas_draw_str(canvas, 2, 24, "ID8268 clones ONLY");
        canvas_draw_str(canvas, 2, 37, "May damage genuine Hitag S");
        canvas_draw_str(canvas, 2, 50, "OK: find tag   Back: cancel");
        break;
    case HitagScreenReading:
        canvas_draw_str(canvas, 2, 29, "Reading and confirming UID...");
        canvas_draw_str(canvas, 2, 46, "Back cancels safely");
        break;
    case HitagScreenReady:
        snprintf(
            line,
            sizeof(line),
            "UID %02X%02X%02X%02X",
            app->uid[0],
            app->uid[1],
            app->uid[2],
            app->uid[3]);
        canvas_draw_str(canvas, 2, 26, line);
        canvas_draw_str(canvas, 2, 41, "OK AGAIN: WRITE pages 4/5");
        canvas_draw_str(canvas, 2, 56, "Back: cancel");
        break;
    case HitagScreenWriteSent:
        canvas_draw_str(canvas, 2, 25, "Write sent - NOT verified");
        canvas_draw_str(canvas, 2, 40, "Verify with external reader");
        canvas_draw_str(canvas, 2, 58, "Back: exit");
        break;
    case HitagScreenError:
        canvas_draw_str(canvas, 2, 27, app->error ? app->error : "Operation failed");
        canvas_draw_str(canvas, 2, 45, "OK: retry   Back: exit");
        break;
    }
    furi_mutex_release(app->mutex);
}

static void hitag_set_screen(HitagWriter* app, ViewPort* viewport, HitagScreen screen) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->screen = screen;
    furi_mutex_release(app->mutex);
    view_port_update(viewport);
}

static bool hitag_abort(void* context) {
    HitagWriter* app = context;
    InputEvent event;
    while(furi_message_queue_get(app->queue, &event, 0) == FuriStatusOk) {
        if(event.key == InputKeyBack &&
           (event.type == InputTypePress || event.type == InputTypeShort)) {
            return true;
        }
    }
    return false;
}

static uint64_t hitag_em4100_frame(const uint8_t id[5]) {
    uint64_t frame = 0x1FF;
    for(size_t byte = 0; byte < 5; byte++) {
        for(int nibble_shift = 4; nibble_shift >= 0; nibble_shift -= 4) {
            const uint8_t nibble = (id[byte] >> nibble_shift) & 0x0F;
            uint8_t parity = 0;
            for(int bit = 3; bit >= 0; bit--) {
                const uint8_t value = (nibble >> bit) & 1;
                parity ^= value;
                frame = (frame << 1) | value;
            }
            frame = (frame << 1) | parity;
        }
    }
    for(uint8_t column = 0; column < 4; column++) {
        uint8_t parity = 0;
        for(uint8_t row = 1; row <= 10; row++) {
            parity ^= (frame >> (row * 5 - 1)) & 1;
        }
        frame = (frame << 1) | parity;
    }
    return frame << 1;
}

static void hitag_make_payload(const uint8_t id[5], LFRFIDHitagS* payload) {
    const uint64_t frame = hitag_em4100_frame(id);
    for(size_t i = 0; i < 4; i++) {
        payload->page4[i] = frame >> (56 - i * 8);
        payload->page5[i] = frame >> (24 - i * 8);
    }
}

int32_t hitags_em4100_app(void* context) {
    UNUSED(context);
    const char* selftest = hitags_selftest();
    if(selftest) {
        FURI_LOG_E(TAG, "transport self-test failed: %s", selftest);
        return 1;
    }

    HitagWriter* app = malloc(sizeof(HitagWriter));
    memset(app, 0, sizeof(HitagWriter));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->screen = HitagScreenEdit;

    ViewPort* viewport = view_port_alloc();
    view_port_draw_callback_set(viewport, hitag_draw_callback, app);
    view_port_input_callback_set(viewport, hitag_input_callback, app->queue);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, viewport, GuiLayerFullscreen);

    bool running = true;
    while(running) {
        InputEvent event;
        if(furi_message_queue_get(app->queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        if(event.type != InputTypePress && event.type != InputTypeRepeat &&
           event.type != InputTypeShort) {
            continue;
        }

        if(event.key == InputKeyBack) {
            if(app->screen == HitagScreenEdit || app->screen == HitagScreenWriteSent ||
               app->screen == HitagScreenError) {
                running = false;
            } else {
                hitag_set_screen(app, viewport, HitagScreenEdit);
            }
            continue;
        }

        if(app->screen == HitagScreenEdit) {
            if(event.key == InputKeyLeft && app->cursor) app->cursor--;
            if(event.key == InputKeyRight && app->cursor < 4) app->cursor++;
            if(event.key == InputKeyUp) app->em4100[app->cursor]++;
            if(event.key == InputKeyDown) app->em4100[app->cursor]--;
            if(event.key == InputKeyOk) hitag_set_screen(app, viewport, HitagScreenWarning);
            view_port_update(viewport);
        } else if(app->screen == HitagScreenWarning && event.key == InputKeyOk) {
            hitag_set_screen(app, viewport, HitagScreenReading);
            if(hitags_read_uid(app->uid, hitag_abort, app)) {
                hitag_set_screen(app, viewport, HitagScreenReady);
            } else {
                app->error = "No confirmed Hitag S UID";
                hitag_set_screen(app, viewport, HitagScreenError);
            }
        } else if(app->screen == HitagScreenReady && event.key == InputKeyOk) {
            LFRFIDHitagS payload;
            hitag_make_payload(app->em4100, &payload);
            hitags_write(&payload, app->uid);
            hitag_set_screen(app, viewport, HitagScreenWriteSent);
        } else if(app->screen == HitagScreenError && event.key == InputKeyOk) {
            hitag_set_screen(app, viewport, HitagScreenWarning);
        }
    }

    gui_remove_view_port(gui, viewport);
    furi_record_close(RECORD_GUI);
    view_port_free(viewport);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
