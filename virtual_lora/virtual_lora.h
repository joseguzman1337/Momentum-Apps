#pragma once

#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/widget.h>
#include <dialogs/dialogs.h>
#include <furi_hal.h>

typedef struct VirtualLora VirtualLora;

typedef enum {
    VirtualLoraViewSubmenu,
    VirtualLoraViewTextBox,
    VirtualLoraViewWidget,
} VirtualLoraView;

typedef enum {
    VirtualLoraEventTypeKey,
    VirtualLoraEventTypeCustom,
} VirtualLoraEventType;

typedef struct {
    VirtualLoraEventType type;
    InputEvent input;
} VirtualLoraEvent;

struct VirtualLora {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    TextBox* text_box;
    Widget* widget;
    DialogsApp* dialogs;
    
    FuriString* text_box_store;
    bool esp32_connected;
    uint32_t detected_signals;
};

typedef enum {
    VirtualLoraSubmenuIndexScan,
    VirtualLoraSubmenuIndexTransmit,
    VirtualLoraSubmenuIndexSettings,
} VirtualLoraSubmenuIndex;

VirtualLora* virtual_lora_alloc();
void virtual_lora_free(VirtualLora* app);