#include "../virtual_lora.h"
#include "virtual_lora_scene.h"
#include "../mcp_client.h"

static McpClient* mcp_client = NULL;

static void transmit_mcp_event_callback(McpEvent* event, void* context) {
    VirtualLora* app = context;
    
    if(event->type == McpEventTypeResponse) {
        furi_string_cat_str(app->text_box_store, "Transmission successful via A2A\n");
        text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
    }
}

void virtual_lora_scene_transmit_on_enter(void* context) {
    VirtualLora* app = context;
    
    furi_string_reset(app->text_box_store);
    furi_string_cat_str(app->text_box_store, "Virtual LoRa Transmit\n");
    furi_string_cat_str(app->text_box_store, "Connecting to ESP32...\n");
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
    
    view_dispatcher_switch_to_view(app->view_dispatcher, VirtualLoraViewTextBox);
    
    // Initialize MCP client
    mcp_client = mcp_client_alloc();
    if(mcp_client_start(mcp_client, transmit_mcp_event_callback, app)) {
        furi_string_cat_str(app->text_box_store, "ESP32 connected.\n");
        furi_string_cat_str(app->text_box_store, "Transmitting test payload via A2A...\n");
        
        // Send test data via Agent-to-Agent communication
        mcp_client_transmit_data(mcp_client, "Hello LoRaWAN Network!");
        
        app->esp32_connected = true;
    } else {
        furi_string_cat_str(app->text_box_store, "Failed to connect to ESP32\n");
        app->esp32_connected = false;
    }
    
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
}

bool virtual_lora_scene_transmit_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeBack) {
        consumed = true;
    }
    
    return consumed;
}

void virtual_lora_scene_transmit_on_exit(void* context) {
    VirtualLora* app = context;
    
    if(mcp_client) {
        mcp_client_stop(mcp_client);
        mcp_client_free(mcp_client);
        mcp_client = NULL;
    }
    
    app->esp32_connected = false;
    text_box_reset(app->text_box);
}