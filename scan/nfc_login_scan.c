#include "nfc_login_scan.h"
#include "../scenes/enroll/enroll_scene.h"
#include "../hid/nfc_login_hid.h"
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>

// Import HAS_BLE_HID_API
#ifndef HAS_BLE_HID_API
    #define HAS_BLE_HID_API 0
#endif

// NFC scanning thread
int32_t app_scan_thread(void* context) {
    App* app = context;
    
    Nfc* nfc = nfc_alloc();
    if(!nfc) {
        FURI_LOG_E(TAG, "Failed to allocate NFC");
        return 0;
    }
    
    AsyncPollerContext async_ctx = {
        .thread_id = furi_thread_get_current_id(),
        .reset_counter = 0,
        .detected = false,
        .error = Iso14443_3aErrorNone,
        .poller = NULL,
    };
    
    while(app->scanning) {
        async_ctx.detected = false;
        async_ctx.error = Iso14443_3aErrorNone;
        
        NfcPoller* poller = nfc_poller_alloc(nfc, NfcProtocolIso14443_3a);
        async_ctx.poller = poller; // Store poller pointer for callback
        nfc_poller_start(poller, iso14443_3a_async_callback, &async_ctx);
        
        // Wait for completion or reset
        furi_thread_flags_wait(
            ISO14443_3A_ASYNC_FLAG_COMPLETE, FuriFlagWaitAny, FuriWaitForever);
        furi_thread_flags_clear(ISO14443_3A_ASYNC_FLAG_COMPLETE);
        
        nfc_poller_stop(poller);
        nfc_poller_free(poller);
        
        // Delay after stopping poller to let field cool down
        // This reduces duty cycle and prevents coil overheating
        furi_delay_ms(NFC_COOLDOWN_DELAY_MS);
        
        if(async_ctx.detected && async_ctx.error == Iso14443_3aErrorNone) {
            size_t uid_len = 0;
            const uint8_t* uid = iso14443_3a_get_uid(&async_ctx.iso14443_3a_data, &uid_len);
            
            if(uid && uid_len > 0) {
                int match_index = -1;
                bool allow_type = false;
                // If a card is actively selected, only allow that exact card to trigger
                if(app->has_active_selection && app->active_card_index < app->card_count) {
                    if(app->cards[app->active_card_index].uid_len == uid_len &&
                       memcmp(app->cards[app->active_card_index].uid, uid, uid_len) == 0) {
                        match_index = (int)app->active_card_index;
                        allow_type = true;
                    } else {
                        // Selected does not match scanned tag: do not type
                        allow_type = false;
                    }
                } else {
                    // No active selection: fallback to first matching card
                    for(size_t i = 0; i < app->card_count; i++) {
                        if(app->cards[i].uid_len == uid_len &&
                           memcmp(app->cards[i].uid, uid, uid_len) == 0) {
                            match_index = (int)i;
                            allow_type = true;
                            break;
                        }
                    }
                }
                
                if(allow_type && match_index >= 0) {
                    // Check if scanning was stopped before starting HID operations
                    if(!app->scanning) break;
                    
                    notification_message(app->notification, &sequence_success);
                    
                    // Determine effective HID mode (fallback to USB if BLE not available)
                    HidMode effective_mode = app->hid_mode;
                    #if !HAS_BLE_HID_API
                    if(effective_mode == HidModeBle) {
                        effective_mode = HidModeUsb;
                    }
                    #endif
                    
                    // Save current USB config (only for USB mode)
                    if(effective_mode == HidModeUsb) {
                        app->previous_usb_config = furi_hal_usb_get_config();
                    } else {
                        app->previous_usb_config = NULL;
                    }
                    
                    // Initialize HID and wait for connection
        
        if(initialize_hid_and_wait_with_mode(effective_mode)) {
                        // Check again after HID initialization
                        if(!app->scanning) {
                            deinitialize_hid_with_restore_and_mode(app->previous_usb_config, effective_mode);
                            app->previous_usb_config = NULL;
                            break;
                        }
                        
                        // Fixed delay after HID connect before typing
                        furi_delay_ms(HID_POST_CONNECT_DELAY_MS);
                        
                        // Check again before typing
                        if(!app->scanning) {
                            deinitialize_hid_with_restore_and_mode(app->previous_usb_config, effective_mode);
                            app->previous_usb_config = NULL;
                            break;
                        }
                        
                        // Ensure no stuck modifiers
                        furi_hal_hid_kb_release_all();
                        
                        // Type the password and wait proportionally to string length
                        uint32_t typed_ms = app_type_password(app, app->cards[match_index].password);
                        
                        // Check after typing
                        if(!app->scanning) {
                            deinitialize_hid_with_restore_and_mode(app->previous_usb_config, effective_mode);
                            app->previous_usb_config = NULL;
                            break;
                        }
                        
                        // Additional delay equal to approximate typing time
                        if(typed_ms > 0) {
                            furi_delay_ms(typed_ms);
                        }
                    } else {
                        notification_message(app->notification, &sequence_error);
                    }
                    
                        // Restore previous USB config regardless (only if still scanning)
                        if(app->scanning) {
                            deinitialize_hid_with_restore_and_mode(app->previous_usb_config, effective_mode);
                            app->previous_usb_config = NULL;
                            // Wait a bit before scanning again
                            furi_delay_ms(HID_POST_TYPE_DELAY_MS);
                        } else {
                            // Scanning was stopped, cleanup and exit
                            deinitialize_hid_with_restore_and_mode(app->previous_usb_config, effective_mode);
                            app->previous_usb_config = NULL;
                            break;
                        }
                } else {
                    // If a selection exists but tag doesn't match, give a brief error buzz
                    if(app->has_active_selection) {
                        notification_message(app->notification, &sequence_error);
                        furi_delay_ms(ERROR_NOTIFICATION_DELAY_MS);
                    }
                }
            }
        }
        
        // Additional delay between scan attempts for lower duty cycle
        // The 50ms delay above + reset cycles (every 3 attempts) should keep field meter readings low
        furi_delay_ms(NFC_SCAN_DELAY_MS);
    }
    
    nfc_free(nfc);
    return 0;
}