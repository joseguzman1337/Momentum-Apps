#include "mcp_client.h"
#include <furi_hal_serial.h>

#define TAG "McpClient"

struct McpClient {
    FuriHalSerialHandle* serial;
    FuriThread* worker_thread;
    FuriStreamBuffer* rx_stream;
    McpEventCallback callback;
    void* callback_context;
    bool running;
    uint32_t request_id;
};

static int32_t mcp_client_worker(void* context) {
    McpClient* client = context;
    uint8_t buffer[MCP_BUFFER_SIZE];
    
    while(client->running) {
        size_t bytes_received = furi_stream_buffer_receive(
            client->rx_stream, buffer, sizeof(buffer) - 1, 100);
        
        if(bytes_received > 0) {
            buffer[bytes_received] = '\0';
            FURI_LOG_D(TAG, "Received: %s", buffer);
            
            // Basic JSON parsing for LoRa detection events
            if(strstr((char*)buffer, "\"method\":\"lora_detected\"")) {
                McpEvent event = {
                    .type = McpEventTypeLoraDetected,
                    .frequency = 868100000, // Parse from JSON
                    .rssi = -75,           // Parse from JSON
                    .confidence = 0.85f    // Parse from JSON
                };
                if(client->callback) {
                    client->callback(&event, client->callback_context);
                }
            }
        }
    }
    
    return 0;
}

static void mcp_client_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    McpClient* client = context;
    
    if(event == FuriHalSerialRxEventData) {
        uint8_t data = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(client->rx_stream, &data, 1, 0);
    }
}

McpClient* mcp_client_alloc() {
    McpClient* client = malloc(sizeof(McpClient));
    
    client->serial = furi_hal_serial_control_acquire(MCP_UART_CH);
    client->rx_stream = furi_stream_buffer_alloc(MCP_BUFFER_SIZE, 1);
    client->worker_thread = furi_thread_alloc_ex("McpWorker", 1024, mcp_client_worker, client);
    client->running = false;
    client->request_id = 1;
    
    return client;
}

void mcp_client_free(McpClient* client) {
    if(client->running) {
        mcp_client_stop(client);
    }
    
    furi_thread_free(client->worker_thread);
    furi_stream_buffer_free(client->rx_stream);
    furi_hal_serial_control_release(client->serial);
    free(client);
}

bool mcp_client_start(McpClient* client, McpEventCallback callback, void* context) {
    client->callback = callback;
    client->callback_context = context;
    
    furi_hal_serial_init(client->serial, MCP_UART_BAUD);
    furi_hal_serial_async_rx_start(client->serial, mcp_client_rx_callback, client, false);
    
    client->running = true;
    furi_thread_start(client->worker_thread);
    
    // Send MCP initialization
    const char* init_msg = "{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"sampling\":{},\"logging\":{}},\"clientInfo\":{\"name\":\"FlipperZero-ESP32-Edge\",\"version\":\"1.0\"}},\"id\":1}\n";
    furi_hal_serial_tx(client->serial, (uint8_t*)init_msg, strlen(init_msg));
    
    return true;
}

void mcp_client_stop(McpClient* client) {
    client->running = false;
    furi_thread_join(client->worker_thread);
    furi_hal_serial_async_rx_stop(client->serial);
    furi_hal_serial_deinit(client->serial);
}

bool mcp_client_scan_spectrum(McpClient* client, uint32_t frequency, uint32_t bandwidth) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
        "{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"cc1101_scan_spectrum\",\"arguments\":{\"frequency_hz\":%lu,\"bandwidth_khz\":%lu,\"duration_ms\":5000}},\"id\":%lu}\n",
        frequency, bandwidth / 1000, client->request_id++);
    
    furi_hal_serial_tx(client->serial, (uint8_t*)buffer, strlen(buffer));
    return true;
}

bool mcp_client_transmit_data(McpClient* client, const char* data) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
        "{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"lora_transmit\",\"arguments\":{\"payload\":\"%s\"}},\"id\":%lu}\n",
        data, client->request_id++);
    
    furi_hal_serial_tx(client->serial, (uint8_t*)buffer, strlen(buffer));
    return true;
}

bool mcp_client_configure_cc1101(McpClient* client, uint32_t frequency, uint8_t modulation) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
        "{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"cc1101_configure\",\"arguments\":{\"frequency_hz\":%lu,\"modulation\":%u}},\"id\":%lu}\n",
        frequency, modulation, client->request_id++);
    
    furi_hal_serial_tx(client->serial, (uint8_t*)buffer, strlen(buffer));
    return true;
}