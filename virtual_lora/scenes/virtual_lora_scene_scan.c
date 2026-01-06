#include "../virtual_lora.h"
#include "virtual_lora_scene.h"
#include "../mcp_client.h"

static McpClient* mcp_client = NULL;

static void scan_mcp_event_callback(McpEvent* event, void* context) {
    VirtualLora* app = context;
    
    if(event->type == McpEventTypeLoraDetected) {
        app->detected_signals++;
        
        FuriString* temp_str = furi_string_alloc();
        furi_string_printf(temp_str, 
            "LoRa Signal Detected!\n"
            "Frequency: %.1f MHz\n"
            "RSSI: %d dBm\n"
            "Confidence: %.0f%%\n"
            "Total Detected: %lu\n",
            (double)((float)event->frequency / 1000000.0f),
            (int)event->rssi,
            (double)(event->confidence * 100.0f),
            (unsigned long)app->detected_signals);
        
        furi_string_cat(app->text_box_store, temp_str);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
        furi_string_free(temp_str);
    }
}

void virtual_lora_scene_scan_on_enter(void* context) {
    VirtualLora* app = context;
    
    furi_string_reset(app->text_box_store);
    furi_string_cat_str(app->text_box_store, "Initializing ESP32 connection...\n");
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
    text_box_set_focus(app->text_box, TextBoxFocusEnd);
    
    view_dispatcher_switch_to_view(app->view_dispatcher, VirtualLoraViewTextBox);
    
    // Initialize MCP client
    mcp_client = mcp_client_alloc();
    if(mcp_client_start(mcp_client, scan_mcp_event_callback, app)) {
        furi_string_cat_str(app->text_box_store, "ESP32 connected. Starting scan...\n");
        app->esp32_connected = true;
        
        // Start scanning EU868 LoRa frequencies
        mcp_client_scan_spectrum(mcp_client, 868100000, 125000); // 868.1 MHz, 125kHz BW
        mcp_client_scan_spectrum(mcp_client, 868300000, 125000); // 868.3 MHz, 125kHz BW
        mcp_client_scan_spectrum(mcp_client, 868500000, 125000); // 868.5 MHz, 125kHz BW
    } else {
        furi_string_cat_str(app->text_box_store, "Failed to connect to ESP32\n");
        app->esp32_connected = false;
    }
    
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
}

bool virtual_lora_scene_scan_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeBack) {
        consumed = true;
    }
    
    return consumed;
}

void virtual_lora_scene_scan_on_exit(void* context) {
    VirtualLora* app = context;
    
    if(mcp_client) {
        mcp_client_stop(mcp_client);
        mcp_client_free(mcp_client);
        mcp_client = NULL;
    }
    
    app->esp32_connected = false;
    text_box_reset(app->text_box);
}