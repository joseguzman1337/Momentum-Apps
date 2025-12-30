#include "../virtual_lora.h"
#include "virtual_lora_scene.h"

void virtual_lora_scene_start_submenu_callback(void* context, uint32_t index) {
    VirtualLora* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void virtual_lora_scene_start_on_enter(void* context) {
    VirtualLora* app = context;
    Submenu* submenu = app->submenu;
    
    submenu_add_item(
        submenu, "Scan Environment", VirtualLoraSubmenuIndexScan, 
        virtual_lora_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu, "Transmit Data", VirtualLoraSubmenuIndexTransmit, 
        virtual_lora_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu, "Settings", VirtualLoraSubmenuIndexSettings, 
        virtual_lora_scene_start_submenu_callback, app);
    
    submenu_set_selected_item(submenu, scene_manager_get_scene_state(app->scene_manager, VirtualLoraSceneStart));
    
    view_dispatcher_switch_to_view(app->view_dispatcher, VirtualLoraViewSubmenu);
}

bool virtual_lora_scene_start_on_event(void* context, SceneManagerEvent event) {
    VirtualLora* app = context;
    bool consumed = false;
    
    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, VirtualLoraSceneStart, event.event);
        consumed = true;
        switch(event.event) {
        case VirtualLoraSubmenuIndexScan:
            scene_manager_next_scene(app->scene_manager, VirtualLoraSceneScan);
            break;
        case VirtualLoraSubmenuIndexTransmit:
            scene_manager_next_scene(app->scene_manager, VirtualLoraSceneTransmit);
            break;
        case VirtualLoraSubmenuIndexSettings:
            scene_manager_next_scene(app->scene_manager, VirtualLoraSceneSettings);
            break;
        }
    }
    
    return consumed;
}

void virtual_lora_scene_start_on_exit(void* context) {
    VirtualLora* app = context;
    submenu_reset(app->submenu);
}