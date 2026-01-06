#include "virtual_lora.h"
#include "scenes/virtual_lora_scene.h"
#include "mcp_client.h"

#define TAG "VirtualLora"

static bool virtual_lora_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    VirtualLora* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool virtual_lora_back_event_callback(void* context) {
    furi_assert(context);
    VirtualLora* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

VirtualLora* virtual_lora_alloc() {
    VirtualLora* app = malloc(sizeof(VirtualLora));
    
    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&virtual_lora_scene_handlers, app);
    
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, virtual_lora_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, virtual_lora_back_event_callback);
    
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, VirtualLoraViewSubmenu, submenu_get_view(app->submenu));
    
    app->text_box = text_box_alloc();
    view_dispatcher_add_view(app->view_dispatcher, VirtualLoraViewTextBox, text_box_get_view(app->text_box));
    app->text_box_store = furi_string_alloc();
    
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, VirtualLoraViewWidget, widget_get_view(app->widget));
    
    app->esp32_connected = false;
    app->detected_signals = 0;
    
    scene_manager_next_scene(app->scene_manager, VirtualLoraSceneStart);
    
    return app;
}

void virtual_lora_free(VirtualLora* app) {
    furi_assert(app);
    
    view_dispatcher_remove_view(app->view_dispatcher, VirtualLoraViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, VirtualLoraViewTextBox);
    view_dispatcher_remove_view(app->view_dispatcher, VirtualLoraViewWidget);
    
    submenu_free(app->submenu);
    text_box_free(app->text_box);
    widget_free(app->widget);
    furi_string_free(app->text_box_store);
    
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);
    
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    
    free(app);
}

int32_t virtual_lora_app(void* p) {
    UNUSED(p);
    VirtualLora* app = virtual_lora_alloc();
    
    view_dispatcher_run(app->view_dispatcher);
    
    virtual_lora_free(app);
    return 0;
}