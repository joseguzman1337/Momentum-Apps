#pragma once

#include <furi.h>
#include <furi_hal.h>

#define MCP_UART_CH (FuriHalSerialIdUsart)
#define MCP_UART_BAUD (921600)
#define MCP_BUFFER_SIZE (512)

typedef struct McpClient McpClient;

typedef enum {
    McpEventTypeConnected,
    McpEventTypeDisconnected,
    McpEventTypeLoraDetected,
    McpEventTypeResponse,
} McpEventType;

typedef struct {
    McpEventType type;
    uint32_t frequency;
    int8_t rssi;
    float confidence;
    char* data;
} McpEvent;

typedef void (*McpEventCallback)(McpEvent* event, void* context);

McpClient* mcp_client_alloc();
void mcp_client_free(McpClient* client);
bool mcp_client_start(McpClient* client, McpEventCallback callback, void* context);
void mcp_client_stop(McpClient* client);
bool mcp_client_scan_spectrum(McpClient* client, uint32_t frequency, uint32_t bandwidth);
bool mcp_client_transmit_data(McpClient* client, const char* data);
bool mcp_client_configure_cc1101(McpClient* client, uint32_t frequency, uint8_t modulation);