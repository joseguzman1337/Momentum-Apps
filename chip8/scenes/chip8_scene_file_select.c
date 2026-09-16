#include "../chip8_app_i.h"
#include "furi_hal_power.h"
#include <assets_icons.h>

static bool chip8_file_select(Chip8App* chip8) {
    furi_assert(chip8);
    chip8->file_name = furi_string_alloc_set_str(CHIP8_APP_PATH_FOLDER);
    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(
        &browser_options, CHIP8_APP_EXTENSION, &I_unknown_10px);
    browser_options.base_path = CHIP8_APP_PATH_FOLDER;
    bool res = dialog_file_browser_show(
        chip8->dialogs, chip8->file_name, chip8->file_name, &browser_options);

    FURI_LOG_I(
        "Chip8_file_browser_show", "chip8->file_name: %s", furi_string_get_cstr(chip8->file_name));
    FURI_LOG_I("Chip8_file_browser_show", "res: %d", res);
    return res;
}

void chip8_scene_file_select_on_enter(void* context) {
    Chip8App* chip8 = context;

    if(chip8_file_select(chip8)) {
        FURI_LOG_I(
            "Chip8", "chip8_file_select, file_name = %s", furi_string_get_cstr(chip8->file_name));
        scene_manager_next_scene(chip8->scene_manager, Chip8WorkView);
    } else {
        view_dispatcher_stop(chip8->view_dispatcher);
    }
}

bool chip8_scene_file_select_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void chip8_scene_file_select_on_exit(void* context) {
    UNUSED(context);
}
