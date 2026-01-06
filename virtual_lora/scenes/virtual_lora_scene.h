#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) id,
typedef enum {
#include "virtual_lora_scene_config.h"
    VirtualLoraSceneNum,
} VirtualLoraScene;
#undef ADD_SCENE

extern const SceneManagerHandlers virtual_lora_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "virtual_lora_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "virtual_lora_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "virtual_lora_scene_config.h"
#undef ADD_SCENE