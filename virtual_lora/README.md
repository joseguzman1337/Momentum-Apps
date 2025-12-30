# Virtual LoRa: AI-Driven LoRa Support for Flipper Zero

This implementation provides "native" LoRa support on Flipper Zero without additional RF hardware, using AI-driven virtualization and the ESP32-S2 WiFi Developer Board.

## Architecture Overview

The system implements a **Neural-RF Bridge** that virtualizes LoRa capabilities through:

- **Flipper Zero**: Runs Virtual LoRa app, configures CC1101 for spectral sensing
- **ESP32-S2**: Performs TinyML inference and MCP communication
- **Cloud Agent**: Handles RAG, A2A communication, and protocol intelligence

## Key Components

### 1. Virtual LoRa App (`/applications/external/virtual_lora/`)
- **Main App**: `virtual_lora.c` - Core application logic
- **MCP Client**: `mcp_client.c` - JSON-RPC communication with ESP32
- **CC1101 Config**: `cc1101_pseudo_lora.c` - Spectral sensing configuration
- **Scenes**: Start, Scan, Transmit, Settings interfaces

### 2. ESP32 Bridge (`/esp32_bridge/`)
- **Main Firmware**: `esp32_mcp_bridge.ino` - Arduino-compatible ESP32 code
- **TinyML Integration**: Real-time LoRa chirp detection
- **MCP Protocol**: JSON-RPC 2.0 communication
- **WiFi Bridge**: Connection to Cloud Agent

### 3. Cloud Agent (`/esp32_bridge/mcp_server.py`)
- **RAG System**: CC1101 configuration retrieval
- **A2A Communication**: LoRaWAN network integration
- **Super Agent**: Orchestrates the entire system

## CC1101 "Pseudo-LoRa" Configuration

The CC1101 is configured for wideband energy detection:

```c
// Key register settings for 868 MHz spectral sensing
{CC1101_MDMCFG2, 0x30, "ASK/OOK Modulation"},
{CC1101_MDMCFG4, 0x2D, "RX Bandwidth: 270 kHz"},
{CC1101_PKTCTRL0, 0x32, "Async Serial Mode"},
{CC1101_AGCCTRL2, 0x07, "AGC Control: Max Gain"},
```

## Installation

### 1. Flipper Zero App
```bash
cd /path/to/Momentum-Firmware
./fbt fap_virtual_lora
```

### 2. ESP32 Firmware
1. Install Arduino IDE with ESP32 support
2. Install libraries: `WiFi`, `WebSocketsClient`, `ArduinoJson`, `TensorFlowLite_ESP32`
3. Flash `esp32_mcp_bridge.ino` to ESP32-S2
4. Connect ESP32 pins:
   - GDO0 (Pin 4) → Flipper GPIO Pin 2
   - TX/RX → Flipper UART pins 13/14

### 3. Cloud Agent
```bash
pip install websockets asyncio requests
python3 mcp_server.py
```

## Usage

1. **Connect Hardware**: ESP32-S2 WiFi Devboard to Flipper Zero
2. **Start Cloud Agent**: Run `mcp_server.py` on local network
3. **Launch App**: Open "Virtual LoRa" from Flipper menu
4. **Scan Environment**: Detects LoRa signals via AI inference
5. **Transmit Data**: Sends data via A2A to LoRaWAN networks

## Technical Details

### MCP Protocol Flow
```
Flipper → ESP32 → Cloud Agent
   ↓         ↓         ↓
JSON-RPC  TinyML   RAG/A2A
```

### Signal Detection Pipeline
1. CC1101 outputs raw energy data via GDO0
2. ESP32 samples at ~1kHz, fills 128-sample buffer
3. TinyML model classifies signal as LoRa chirp
4. If confidence > 80%, notifies Flipper and Cloud Agent

### A2A Transmission
1. User enters data in Flipper app
2. ESP32 forwards to Cloud Agent via WebSocket
3. Agent communicates with LoRaWAN network (TTN/Helium)
4. Confirms "transmission" back to Flipper

## Limitations

- **Detection Only**: Cannot decode LoRa payloads (hardware limitation)
- **Transmission Simulation**: Uses A2A, not actual RF transmission
- **Power Consumption**: ESP32 WiFi increases battery drain
- **Model Accuracy**: TinyML model requires training on real LoRa signals

## Future Enhancements

- **Wake-on-Radio**: Use CC1101 WoR for power optimization
- **Better ML Models**: Train on diverse LoRa spreading factors
- **Local A2A**: Direct gateway communication without cloud
- **Multi-Region**: Support US915, AS923, etc.

## Files Structure

```
virtual_lora/
├── application.fam              # App manifest
├── virtual_lora.h/.c           # Main application
├── mcp_client.h/.c             # MCP JSON-RPC client
├── cc1101_pseudo_lora.h/.c     # CC1101 configuration
└── scenes/                     # UI scenes
    ├── virtual_lora_scene_*.c
    └── virtual_lora_scene.h

esp32_bridge/
├── esp32_mcp_bridge.ino        # ESP32 firmware
└── mcp_server.py               # Cloud agent
```

This implementation demonstrates how AI can extend hardware capabilities beyond physical limitations, creating "virtual" protocol support through intelligent software layers.