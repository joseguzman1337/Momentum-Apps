#include "virtual_lora_scene.h"

// Generate scene on_enter handlers array
static void (*const virtual_lora_scene_on_enter_handlers[])(void*) = {
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
#include "virtual_lora_scene_config.h"
#undef ADD_SCENE
};

// Generate scene on_event handlers array
static bool (*const virtual_lora_scene_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
#include "virtual_lora_scene_config.h"
#undef ADD_SCENE
};

// Generate scene on_exit handlers array
static void (*const virtual_lora_scene_on_exit_handlers[])(void*) = {
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
#include "virtual_lora_scene_config.h"
#undef ADD_SCENE
};

// Initialize scene handlers
const SceneManagerHandlers virtual_lora_scene_handlers = {
    .on_enter_handlers = virtual_lora_scene_on_enter_handlers,
    .on_event_handlers = virtual_lora_scene_on_event_handlers,
    .on_exit_handlers = virtual_lora_scene_on_exit_handlers,
    .scene_num = VirtualLoraSceneNum,
};