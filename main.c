#include "nfc_login_app.h"
#include "crypto/nfc_login_crypto.h"
#include "crypto/nfc_login_passcode.h"
#include "settings/nfc_login_settings.h"
#include "storage/nfc_login_card_storage.h"
#include "scan/nfc_login_scan.h"
#include "hid/nfc_login_hid.h"
#include "scenes/scene_manager.h"
#include "scenes/cards/edit_callbacks.h"
#include "scenes/settings/settings_scene.h"
#include "scenes/settings/passcode_canvas.h"

// Check if we're building for Momentum firmware
#ifndef HAS_MOMENTUM_SUPPORT
#ifdef FW_ORIGIN_Momentum
#define HAS_MOMENTUM_SUPPORT
#endif
#endif




// Submenu callbacks

// Main app
int32_t nfc_login(void* p) {
    UNUSED(p);
    
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    
    // Initialize GUI
    app->gui = furi_record_open(RECORD_GUI);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    
    // Views / dispatcher
    scene_manager_init(app);
    
    // Initialize layout settings
    // Load settings (includes keyboard layout)
    app_load_settings(app);
    
    // Start BLE advertising if BLE mode is selected
    if(app->hid_mode == HidModeBle) {
        app_start_ble_advertising();
    }
    
    // Ensure crypto key exists early (will generate if needed)
    // This prevents blocking during save operations
    ensure_crypto_key();
    
    // Initialize passcode prompt state
    app->passcode_prompt_active = false;
    app->passcode_sequence_len = 0;
    app->passcode_needed = false;
    app->passcode_failed_attempts = 0;
    memset(app->passcode_sequence, 0, sizeof(app->passcode_sequence));
    
    // Check if passcode is disabled for NFC Login app
    if(app->passcode_disabled) {
        // Passcode disabled - load cards and start normally
        FURI_LOG_I(TAG, "nfc_login: Passcode disabled, loading cards directly");
        app_load_cards(app);
        FURI_LOG_I(TAG, "nfc_login: Loaded %zu cards", app->card_count);
        app_switch_to_view(app, ViewSubmenu);
    } else {
        // Check if passcode is set
        bool has_passcode_set = has_passcode();
        FURI_LOG_I(TAG, "nfc_login: Passcode set: %s", has_passcode_set ? "true" : "false");
        
        if(has_passcode_set) {
            // Show lockscreen to verify passcode
            FURI_LOG_I(TAG, "nfc_login: Showing lockscreen");
            app->passcode_prompt_active = true;
            app->passcode_sequence_len = 0;
            memset(app->passcode_sequence, 0, sizeof(app->passcode_sequence));
            app->widget_state = 7; // Lockscreen state
            app_switch_to_view(app, ViewPasscodeCanvas);
        } else {
            // No passcode set - show setup prompt on first boot
            FURI_LOG_I(TAG, "nfc_login: No passcode set, showing setup prompt");
            app->passcode_prompt_active = true;
            app->passcode_sequence_len = 0;
            memset(app->passcode_sequence, 0, sizeof(app->passcode_sequence));
            app->widget_state = 6; // Passcode setup state
            app_switch_to_view(app, ViewPasscodeCanvas);
        }
    }
    
    // Run dispatcher
    view_dispatcher_run(app->view_dispatcher);
    
    // Cleanup
    if(app->scanning) {
        app->scanning = false;
        if(app->scan_thread) {
            furi_thread_join(app->scan_thread);
            furi_thread_free(app->scan_thread);
        }
    }
    if(app->enrollment_scanning) {
        app->enrollment_scanning = false;
        if(app->enroll_scan_thread) {
            furi_thread_join(app->enroll_scan_thread);
            furi_thread_free(app->enroll_scan_thread);
        }
    }
    
    if(app->previous_usb_config || app->hid_mode == HidModeBle) {
        deinitialize_hid_with_restore_and_mode(app->previous_usb_config, app->hid_mode);
    }
    
    view_dispatcher_remove_view(app->view_dispatcher, ViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, ViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, ViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, ViewFileBrowser);
    view_dispatcher_remove_view(app->view_dispatcher, ViewByteInput);
    view_dispatcher_remove_view(app->view_dispatcher, ViewPasscodeCanvas);
    
    submenu_free(app->submenu);
    text_input_free(app->text_input);
    widget_free(app->widget);
    file_browser_free(app->file_browser);
    furi_string_free(app->fb_output_path);
    byte_input_free(app->byte_input);
    passcode_canvas_view_free(app->passcode_canvas_view);
    view_dispatcher_free(app->view_dispatcher);
    
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    
    free(app);
    
    return 0;
}