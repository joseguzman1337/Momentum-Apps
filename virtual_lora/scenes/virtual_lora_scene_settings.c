#include "../virtual_lora.h"
#include "virtual_lora_scene.h"

void virtual_lora_scene_settings_on_enter(void* context) {
    VirtualLora* app = context;
    
    widget_reset(app->widget);
    FuriString* temp_str = furi_string_alloc();
    furi_string_printf(temp_str,
        "\e#Virtual LoRa Settings\e#\n"
        "ESP32 Status: %s\n"
        "Detected Signals: %lu\n"
        "MCP Protocol: v2024-11-05\n"
        "AI Stack: Always On\n"
        "\n"
        "Press Back to return",
        app->esp32_connected ? "Connected" : "Disconnected",
        app->detected_signals);
    
    widget_add_text_box_element(
        app->widget, 0, 0, 128, 64,
        AlignCenter, AlignTop,
        furi_string_get_cstr(temp_str),
        false);
    
    furi_string_free(temp_str);
    
    view_dispatcher_switch_to_view(app->view_dispatcher, VirtualLoraViewWidget);
}

bool virtual_lora_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeBack) {
        consumed = true;
    }
    
    return consumed;
}

void virtual_lora_scene_settings_on_exit(void* context) {
    VirtualLora* app = context;
    widget_reset(app->widget);
}