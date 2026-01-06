#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>

#define TAG "CognitiveAgent"

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    Storage* storage;
} CognitiveApp;

static void cognitive_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 10, 20, "Cognitive Agent");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 10, 35, "Status: Brainstem Active");
    canvas_draw_str(canvas, 10, 50, "Coprocessor: Monitoring UART");
}

static void cognitive_input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    FuriMessageQueue* event_queue = context;
    furi_message_queue_put(event_queue, event, FuriWaitForever);
}

static bool cognitive_init_knowledge_base(Storage* storage) {
    FURI_LOG_I(TAG, "Initializing Knowledge Base...");
    if (!storage_simply_mkdir(storage, "/ext/knowledge")) {
        FURI_LOG_W(TAG, "Knowledge base directory already exists or failed to create");
    }
    return true;
}

int32_t cognitive_agent_app(void* p) {
    UNUSED(p);
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    CognitiveApp* app = malloc(sizeof(CognitiveApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, cognitive_draw_callback, app);
    view_port_input_callback_set(app->view_port, cognitive_input_callback, event_queue);
    
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    cognitive_init_knowledge_base(app->storage);

    FURI_LOG_I(TAG, "Cognitive Agent Started. Awaiting ESP32 Neocortex...");

    InputEvent event;
    while(1) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypeShort && event.key == InputKeyBack) {
                break;
            }
        }
    }

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(event_queue);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);

    return 0;
}
