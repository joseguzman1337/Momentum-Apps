#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_nfc.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_hid.h>
#include <furi_hal_version.h>
#include <furi_hal_crypto.h>
#include <usb_hid.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/widget.h>
#include <gui/modules/file_browser.h>
#include <gui/modules/byte_input.h>
#include <input/input.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller_sync.h>
#include <assets_icons.h>
#include <string.h>
#include <stdlib.h>

// Check if we're building for Momentum firmware
#ifndef HAS_MOMENTUM_SUPPORT
#ifdef FW_ORIGIN_Momentum
#define HAS_MOMENTUM_SUPPORT
#endif
#endif

#define TAG "nfc_login"
#define APP_DATA_DIR "/ext/apps_data/nfc_login"
#define NFC_CARDS_FILE_ENC APP_DATA_DIR "/cards.enc"
#define NFC_SETTINGS_FILE APP_DATA_DIR "/settings.txt"
#define BADUSB_LAYOUTS_DIR "/ext/badusb/assets/layouts"
#define CRYPTO_KEY_SLOT FURI_HAL_CRYPTO_ENCLAVE_UNIQUE_KEY_SLOT
#define AES_BLOCK_SIZE 16
#define MAX_CARDS 50
#define MAX_UID_LEN 10
#define MAX_PASSWORD_LEN 64
#define MAX_LAYOUT_PATH 256
#define MAX_ENCRYPTED_SIZE 4096
#define SETTINGS_MENU_ITEMS 4
#define SETTINGS_VISIBLE_ITEMS 3
#define SETTINGS_HELP_Y_POS 54
#define CREDITS_PAGES 2
#define MAX_LAYOUTS 30
#define CARD_LIST_VISIBLE_ITEMS 4
#define KEY_MOD_LEFT_SHIFT 0x02


// Timing constants
#define KEY_PRESS_DELAY_MS 10
#define KEY_RELEASE_DELAY_MS 10
#define ENTER_PRESS_DELAY_MS 50
#define ENTER_RELEASE_DELAY_MS 50
#define NFC_SCAN_DELAY_MS 500
#define NFC_ENROLL_SCAN_DELAY_MS 150
#define NFC_COOLDOWN_DELAY_MS 50
#define HID_SETTLE_DELAY_MS 100
#define HID_INIT_DELAY_MS 25
#define CRYPTO_SETTLE_DELAY_MS 150
#define STORAGE_READ_DELAY_MS 100
#define HID_POST_CONNECT_DELAY_MS 1000
#define HID_POST_TYPE_DELAY_MS 1000
#define ERROR_NOTIFICATION_DELAY_MS 300
#define HID_CONNECT_TIMEOUT_MS 5000
#define HID_CONNECT_RETRY_MS 100

typedef enum {
    EnrollmentStateNone,
    EnrollmentStateName,
    EnrollmentStatePassword,
} EnrollmentState;

typedef struct {
    uint8_t uid[MAX_UID_LEN];
    size_t uid_len;
    char password[MAX_PASSWORD_LEN];
    char name[32];
} NfcCard;

typedef enum { EditStateNone = 0, EditStateName = 1, EditStatePassword = 2, EditStateUid = 3 } EditState;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextInput* text_input;
    Widget* widget;
    FileBrowser* file_browser;
    FuriString* fb_output_path;
    ByteInput* byte_input;
    NotificationApp* notification;
    
    NfcCard cards[MAX_CARDS];
    size_t card_count;
    size_t selected_card;
    // Active selection chosen from list to be used on scan
    bool has_active_selection;
    size_t active_card_index;
    // Edit menu state
    uint8_t edit_menu_index;
    size_t edit_card_index;
    EditState edit_state;
    char edit_uid_text[MAX_UID_LEN * 2 + 1];
    uint8_t edit_uid_bytes[MAX_UID_LEN];
    uint8_t edit_uid_len;
    
    bool scanning;
    FuriThread* scan_thread;
    FuriHalUsbInterface* previous_usb_config;
    
    // Enrollment state
    EnrollmentState enrollment_state;
    NfcCard enrollment_card;
    
    // Track current active view to handle back navigation logic
    uint32_t current_view;
    
    // Widget mode: 0-none, 1-scan, 2-list
    uint8_t widget_state;
    
    // Enrollment scanning thread/flag
    bool enrollment_scanning;
    FuriThread* enroll_scan_thread;
    
    // Settings
    bool append_enter;
    uint16_t input_delay_ms; // Delay between each key press: 10, 50, 100, or 200ms
    uint8_t settings_menu_index; // For navigating settings menu
    uint8_t settings_scroll_offset; // Scroll offset for settings menu (0-1, shows 3 items at a time)
    uint8_t card_list_scroll_offset; // Scroll offset for card list (shows 4 items at a time)
    char keyboard_layout[64]; // Keyboard layout filename (e.g., "en-US.kl")
    bool layout_loaded; // Whether layout is currently loaded
    bool selecting_keyboard_layout; // Flag to track if we're selecting a keyboard layout
    uint16_t layout[128]; // Keycode array: modifiers encoded in upper byte
    uint8_t credits_page; // Current credits page (0-2)
} App;

#define KEY_MOD_LEFT_SHIFT 0x02

typedef enum {
    ViewSubmenu,
    ViewTextInput,
    ViewWidget,
    ViewFileBrowser,
    ViewByteInput,
} ViewId;

typedef enum {
    SubmenuAddCard,
    SubmenuListCards,
    SubmenuStartScan,
    SubmenuSettings,
} SubmenuIndex;

typedef enum {
    EventAddCardStart = 1,
    EventStartScan = 2,
    EventPromptPassword = 3,
    EventEditUidDone = 4,
} AppEvent;

// Forward declarations
static int32_t app_scan_thread(void* context);
static uint32_t app_type_password(App* app, const char* password);
static bool app_save_cards(App* app);
static void app_load_cards(App* app);
static void app_save_settings(App* app);
static void app_load_settings(App* app);
static void app_load_keyboard_layout(App* app);
static void app_text_input_result_callback(void* context);
static bool app_custom_event_callback(void* context, uint32_t event);
static bool app_navigation_callback(void* context);
static void app_file_browser_callback(void* context);
static bool app_import_nfc_file(App* app, const char* path);
static void app_edit_text_result_callback(void* context);
static void app_edit_uid_byte_input_done(void* context);
// Helper function forward declarations
static void uid_to_hex(const uint8_t* uid, size_t uid_len, char* hex_out);
static void app_ensure_data_dir(Storage* storage);
static void app_render_edit_menu(App* app);
static void app_render_card_list(App* app);
static void app_render_credits(App* app);
static void app_render_settings(App* app);

static void app_switch_to_view(App* app, uint32_t view_id) {
    app->current_view = view_id;
    view_dispatcher_switch_to_view(app->view_dispatcher, view_id);
}

// Async poller context for ISO14443-3A
#define ISO14443_3A_ASYNC_FLAG_COMPLETE (1UL << 0)

typedef struct {
    Iso14443_3aData iso14443_3a_data;
    Iso14443_3aError error;
    bool detected;
    FuriThreadId thread_id;
    uint32_t reset_counter;
    NfcPoller* poller; // Store poller pointer to access data via public API
} AsyncPollerContext;

// Async callback for ISO14443-3A poller - uses worker thread with proper field management
static NfcCommand iso14443_3a_async_callback(NfcGenericEvent event, void* context) {
    AsyncPollerContext* ctx = (AsyncPollerContext*)context;
    
    if(event.protocol == NfcProtocolIso14443_3a) {
        Iso14443_3aPollerEvent* poller_event = (Iso14443_3aPollerEvent*)event.event_data;
        
        if(poller_event->type == Iso14443_3aPollerEventTypeReady) {
            // Use public API to get data from NfcPoller
            const NfcDeviceData* device_data = nfc_poller_get_data(ctx->poller);
            if(device_data) {
                const Iso14443_3aData* poller_data = (const Iso14443_3aData*)device_data;
                iso14443_3a_copy(&ctx->iso14443_3a_data, poller_data);
                ctx->error = Iso14443_3aErrorNone;
                ctx->detected = true;
                furi_thread_flags_set(ctx->thread_id, ISO14443_3A_ASYNC_FLAG_COMPLETE);
                return NfcCommandStop;
            }
        } else if(poller_event->type == Iso14443_3aPollerEventTypeError) {
            ctx->error = poller_event->data->error;
            ctx->detected = false;
            // Trigger reset cycle more frequently (every 3 attempts) for coil cooling
            // This matches the NFC app's behavior of cycling the field off periodically
            ctx->reset_counter++;
            if(ctx->reset_counter >= 3) {
                ctx->reset_counter = 0;
                furi_thread_flags_set(ctx->thread_id, ISO14443_3A_ASYNC_FLAG_COMPLETE);
                return NfcCommandReset; // Triggers reset cycle in worker thread (100ms field off)
            }
            furi_thread_flags_set(ctx->thread_id, ISO14443_3A_ASYNC_FLAG_COMPLETE);
            return NfcCommandStop;
        }
    }
    
    return NfcCommandContinue;
}

// Enrollment scanning thread: keep polling until a tag is read or cancelled
static int32_t app_enroll_scan_thread(void* context) {
    App* app = context;
    Nfc* nfc = nfc_alloc();
    if(!nfc) return 0;
    
    AsyncPollerContext async_ctx = {
        .thread_id = furi_thread_get_current_id(),
        .reset_counter = 0,
        .detected = false,
        .error = Iso14443_3aErrorNone,
        .poller = NULL,
    };
    
    while(app->enrollment_scanning) {
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
        
        if(!app->enrollment_scanning) break;
        
        if(async_ctx.detected && async_ctx.error == Iso14443_3aErrorNone) {
            size_t uid_len = 0;
            const uint8_t* uid = iso14443_3a_get_uid(&async_ctx.iso14443_3a_data, &uid_len);
            if(uid && uid_len > 0 && uid_len <= MAX_UID_LEN) {
                app->enrollment_card.uid_len = uid_len;
                memcpy(app->enrollment_card.uid, uid, uid_len);
                notification_message(app->notification, &sequence_success);
                app->enrollment_scanning = false;
                // If we are in edit menu, signal UID edit done; else proceed with add flow
                if(app->widget_state == 3) {
                    view_dispatcher_send_custom_event(app->view_dispatcher, EventEditUidDone);
                } else {
                    // Proceed to password prompt on UI thread via custom event
                    view_dispatcher_send_custom_event(app->view_dispatcher, EventPromptPassword);
                }
                break;
            }
        }
        
        // Delay between scan attempts - async API uses worker thread with proper field management
        furi_delay_ms(NFC_ENROLL_SCAN_DELAY_MS);
    }
    nfc_free(nfc);
    return 0;
}

// Handle long back on main menu to exit the app
// Navigation callback: exit app on Back when on main menu
static bool app_navigation_callback(void* context) {
    App* app = context;
    if(app->current_view == ViewSubmenu) {
        // Ensure all background threads are stopped before exiting
        if(app->enrollment_scanning) {
            app->enrollment_scanning = false;
            if(app->enroll_scan_thread) {
                furi_thread_join(app->enroll_scan_thread);
                furi_thread_free(app->enroll_scan_thread);
                app->enroll_scan_thread = NULL;
            }
        }
        if(app->scanning) {
            app->scanning = false;
            if(app->scan_thread) {
                furi_thread_join(app->scan_thread);
                furi_thread_free(app->scan_thread);
                app->scan_thread = NULL;
            }
        }
        view_dispatcher_stop(app->view_dispatcher);
        return true;
    } else if(app->current_view == ViewTextInput) {
        // On keyboard back: return to the appropriate previous page
        if(app->edit_state != EditStateNone) {
            // Return to edit menu
            app->edit_state = EditStateNone;
            app->widget_state = 3;
            app_render_edit_menu(app);
            app_switch_to_view(app, ViewWidget);
        } else if(app->enrollment_state != EnrollmentStateNone) {
            // Cancel enrollment text input flow and return to submenu
            app->enrollment_state = EnrollmentStateNone;
            app_switch_to_view(app, ViewSubmenu);
        } else {
            // Default: back to submenu
            app_switch_to_view(app, ViewSubmenu);
        }
        return true;
    } else if(app->current_view == ViewFileBrowser) {
        // Leaving file browser returns to list view
        file_browser_stop(app->file_browser);
        app_switch_to_view(app, ViewWidget);
        return true;
    } else if(app->current_view == ViewByteInput) {
        // Back from hex keyboard to edit menu
        app->edit_state = EditStateNone;
        app->widget_state = 3;
        app_render_edit_menu(app);
        app_switch_to_view(app, ViewWidget);
        return true;
    } else if(app->current_view == ViewWidget) {
        // Leaving widget: stop enrollment scan if running
        if(app->enrollment_scanning) {
            app->enrollment_scanning = false;
            if(app->enroll_scan_thread) {
                furi_thread_join(app->enroll_scan_thread);
                furi_thread_free(app->enroll_scan_thread);
                app->enroll_scan_thread = NULL;
            }
        }
        // If widget didn't consume Back, go to submenu
        app_switch_to_view(app, ViewSubmenu);
        return true;
    }
    return false;
}

static void app_edit_text_result_callback(void* context) {
    App* app = context;
    if(app->edit_state == EditStateName || app->edit_state == EditStatePassword) {
        if(app_save_cards(app)) {
            notification_message(app->notification, &sequence_success);
        } else {
            notification_message(app->notification, &sequence_error);
            FURI_LOG_E(TAG, "Failed to save card after edit");
        }
        app->edit_state = EditStateNone;
        // Return to edit menu
        app->widget_state = 3;
        app_render_edit_menu(app);
        app_switch_to_view(app, ViewWidget);
    } else if(app->edit_state == EditStateUid) {
        // Parse hex string from app->edit_uid_text into UID bytes
        const char* p = app->edit_uid_text;
        uint8_t uid[MAX_UID_LEN] = {0};
        size_t idx = 0;
        // skip spaces and parse two hex chars per byte
        while(*p && idx < MAX_UID_LEN) {
            while(*p == ' ') p++;
            if(!isxdigit((unsigned char)p[0])) break;
            unsigned int byte_val = 0;
            if(sscanf(p, "%2x", &byte_val) == 1) {
                uid[idx++] = (uint8_t)byte_val;
                // advance over two hex digits if present
                p++;
                if(isxdigit((unsigned char)*p)) p++;
            } else {
                break;
            }
            while(*p == ' ') p++;
        }
        if(idx > 0) {
            app->cards[app->edit_card_index].uid_len = idx;
            memcpy(app->cards[app->edit_card_index].uid, uid, idx);
            if(app_save_cards(app)) {
                notification_message(app->notification, &sequence_success);
            } else {
                notification_message(app->notification, &sequence_error);
                FURI_LOG_E(TAG, "Failed to save card after UID edit");
            }
        } else {
            notification_message(app->notification, &sequence_error);
        }
        app->edit_state = EditStateNone;
        // Return to edit menu
        app->widget_state = 3;
        widget_reset(app->widget);
        widget_add_string_element(app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Edit Card");
        const char* items[] = {"Name", "Password", "UID", "Delete"};
        for(size_t i = 0; i < 4; i++) {
            char line[32];
            snprintf(line, sizeof(line), "%s %s", (i == app->edit_menu_index) ? ">" : " ", items[i]);
            widget_add_string_element(app->widget, 0, 12 + i * 12, AlignLeft, AlignTop, FontSecondary, line);
        }
        app_switch_to_view(app, ViewWidget);
    }
}

static void app_edit_uid_byte_input_done(void* context) {
    App* app = context;
    // Copy edited bytes into card UID using preset length
    if(app->edit_card_index < app->card_count && app->edit_uid_len > 0 && app->edit_uid_len <= MAX_UID_LEN) {
        app->cards[app->edit_card_index].uid_len = app->edit_uid_len;
        memcpy(app->cards[app->edit_card_index].uid, app->edit_uid_bytes, app->edit_uid_len);
        if(app_save_cards(app)) {
            notification_message(app->notification, &sequence_success);
        } else {
            notification_message(app->notification, &sequence_error);
            FURI_LOG_E(TAG, "Failed to save card after UID byte edit");
        }
    } else {
        notification_message(app->notification, &sequence_error);
    }
    app->edit_state = EditStateNone;
    // Return to edit menu
    app->widget_state = 3;
    app_render_edit_menu(app);
    app_switch_to_view(app, ViewWidget);
}
static void app_file_browser_callback(void* context) {
    App* app = context;
    const char* path = furi_string_get_cstr(app->fb_output_path);
    
    // Handle keyboard layout selection
    if(app->selecting_keyboard_layout) {
        if(!path || path[0] == '\0') {
            app->selecting_keyboard_layout = false;
            app->widget_state = 4;
            app_render_settings(app);
            app_switch_to_view(app, ViewWidget);
            return;
        }
        
        const char* filename = strrchr(path, '/');
        filename = filename ? filename + 1 : path;
        
        if(strstr(filename, ".kl") != NULL) {
            strncpy(app->keyboard_layout, filename, sizeof(app->keyboard_layout) - 1);
            app->keyboard_layout[sizeof(app->keyboard_layout) - 1] = '\0';
            app_save_settings(app);
            notification_message(app->notification, &sequence_success);
        } else {
            notification_message(app->notification, &sequence_error);
        }
        
        app->selecting_keyboard_layout = false;
        app->widget_state = 4;
        app_render_settings(app);
        app_switch_to_view(app, ViewWidget);
        return;
    }
    
    if(!path || path[0] == '\0') {
        app_switch_to_view(app, ViewWidget);
        return;
    }
    
    // Handle NFC file import (original behavior)
    if(app_import_nfc_file(app, path)) {
        // Proceed to ask for password for the imported UID
        app->enrollment_state = EnrollmentStatePassword;
        memset(app->enrollment_card.password, 0, sizeof(app->enrollment_card.password));
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "Enter Password");
        text_input_set_result_callback(
            app->text_input,
            app_text_input_result_callback,
            app,
            app->enrollment_card.password,
            sizeof(app->enrollment_card.password),
            true);
#ifdef HAS_MOMENTUM_SUPPORT
        text_input_show_illegal_symbols(app->text_input, true);
#endif
        app_switch_to_view(app, ViewTextInput);
    } else {
        // Failed to import; return to list
        notification_message(app->notification, &sequence_error);
        app_switch_to_view(app, ViewWidget);
    }
}

static bool app_import_nfc_file(App* app, const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[128];
        size_t len = 0;
        uint8_t ch = 0;
        memset(&app->enrollment_card, 0, sizeof(NfcCard));
        while(true) {
            len = 0;
            while(len < sizeof(line) - 1) {
                uint16_t rd = storage_file_read(file, &ch, 1);
                if(rd == 0) break;
                if(ch == '\n') break;
                line[len++] = (char)ch;
            }
            if(len == 0) break;
            line[len] = '\0';
            if(len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';
            if(strncmp(line, "UID:", 4) == 0) {
                const char* p = line + 4;
                while(*p == ' ') p++;
                size_t idx = 0;
                while(*p && idx < MAX_UID_LEN) {
                    while(*p == ' ') p++;
                    if(!isxdigit((unsigned char)p[0])) break;
                    unsigned int byte_val = 0;
                    if(sscanf(p, "%2x", &byte_val) == 1) {
                        app->enrollment_card.uid[idx++] = (uint8_t)byte_val;
                        p++;
                        if(isxdigit((unsigned char)*p)) p++;
                    } else {
                        break;
                    }
                    while(*p == ' ') p++;
                }
                if(idx > 0) {
                    app->enrollment_card.uid_len = idx;
                    ok = true;
                    break;
                }
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    if(ok) {
        const char* slash = strrchr(path, '/');
        const char* fname = slash ? slash + 1 : path;
        size_t n = 0;
        while(fname[n] && fname[n] != '.' && n < sizeof(app->enrollment_card.name) - 1) {
            app->enrollment_card.name[n] = fname[n];
            n++;
        }
        app->enrollment_card.name[n] = '\0';
    }
    return ok;
}
static bool app_custom_event_callback(void* context, uint32_t event) {
    App* app = context;
    switch(event) {
    case EventAddCardStart:
        // Initialize enrollment and show name input
        memset(&app->enrollment_card, 0, sizeof(NfcCard));
        app->enrollment_state = EnrollmentStateName;
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "Enter Card Name");
        text_input_set_result_callback(
            app->text_input,
            app_text_input_result_callback,
            app,
            app->enrollment_card.name,
            sizeof(app->enrollment_card.name),
            true);
#ifdef HAS_MOMENTUM_SUPPORT
        text_input_show_illegal_symbols(app->text_input, true);
#endif
        app_switch_to_view(app, ViewTextInput);
        return true;
    case EventStartScan: {
        // Show scanning view and start continuous enrollment scan
        widget_reset(app->widget);
        // Use built-in scanning image
        widget_add_icon_element(app->widget, 2, 6, &I_Scanning_123x52);
        widget_add_string_element(app->widget, 0, 56, AlignLeft, AlignTop, FontSecondary, "Back=Cancel");
        app_switch_to_view(app, ViewWidget);
        app->widget_state = 1;
        // Start thread if not already
        if(!app->enrollment_scanning) {
            app->enrollment_scanning = true;
            app->enroll_scan_thread = furi_thread_alloc();
            furi_thread_set_name(app->enroll_scan_thread, "EnrollScan");
            furi_thread_set_stack_size(app->enroll_scan_thread, 2 * 1024);
            furi_thread_set_context(app->enroll_scan_thread, app);
            furi_thread_set_callback(app->enroll_scan_thread, app_enroll_scan_thread);
            furi_thread_start(app->enroll_scan_thread);
        }
        return true;
    }
    case EventPromptPassword:
        // Configure password input
        app->enrollment_state = EnrollmentStatePassword;
        memset(app->enrollment_card.password, 0, sizeof(app->enrollment_card.password));
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "Enter Password");
        text_input_set_result_callback(
            app->text_input,
            app_text_input_result_callback,
            app,
            app->enrollment_card.password,
            sizeof(app->enrollment_card.password),
            true);
#ifdef HAS_MOMENTUM_SUPPORT
        text_input_show_illegal_symbols(app->text_input, true);
#endif
        app_switch_to_view(app, ViewTextInput);
        return true;
    case EventEditUidDone:
        // Write scanned UID into the selected card and save
        if(app->edit_card_index < app->card_count &&
           app->enrollment_card.uid_len > 0 &&
           app->enrollment_card.uid_len <= MAX_UID_LEN) {
            app->cards[app->edit_card_index].uid_len = app->enrollment_card.uid_len;
            memcpy(app->cards[app->edit_card_index].uid, app->enrollment_card.uid, app->enrollment_card.uid_len);
            if(!app_save_cards(app)) {
                FURI_LOG_E(TAG, "Failed to save card after UID scan");
            }
        }
        // Return to edit menu
        app->widget_state = 3;
        widget_reset(app->widget);
        widget_add_string_element(app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Edit Card");
        {
            const char* items[] = {"Name", "Password", "UID (scan)", "Delete"};
            for(size_t i = 0; i < 4; i++) {
                char line[32];
                snprintf(line, sizeof(line), "%s %s", (i == app->edit_menu_index) ? ">" : " ", items[i]);
                widget_add_string_element(app->widget, 0, 12 + i * 12, AlignLeft, AlignTop, FontSecondary, line);
            }
        }
        widget_add_string_element(app->widget, 0, 60, AlignLeft, AlignTop, FontSecondary, "Back=List");
        app_switch_to_view(app, ViewWidget);
        return true;
    default:
        return false;
    }
}

// BadUSB functions (based on password manager example)

static void deinitialize_hid_with_restore(FuriHalUsbInterface* previous_config) {
    furi_hal_hid_kb_release_all();
    furi_delay_ms(HID_INIT_DELAY_MS);
    if(previous_config) {
        furi_hal_usb_set_config(previous_config, NULL);
    } else {
        furi_hal_usb_unlock();
    }
    furi_delay_ms(HID_SETTLE_DELAY_MS);
}

// Initialize HID and wait for host connection
static bool initialize_hid_and_wait(void) {
    furi_hal_usb_unlock();
    if(!furi_hal_usb_set_config(&usb_hid, NULL)) {
        return false;
    }
    // Wait up to HID_CONNECT_TIMEOUT_MS for HID to connect
    uint8_t retries = HID_CONNECT_TIMEOUT_MS / HID_CONNECT_RETRY_MS;
    for(uint8_t i = 0; i < retries; i++) {
        if(furi_hal_hid_is_connected()) {
            return true;
        }
        furi_delay_ms(HID_CONNECT_RETRY_MS);
    }
    return furi_hal_hid_is_connected();
}

// Load keyboard layout - builds default US layout, optionally loads from file
static void app_load_keyboard_layout(App* app) {
    // Initialize all to NONE
    for(int i = 0; i < 128; i++) {
        app->layout[i] = HID_KEYBOARD_NONE;
    }
    
    // Build default US keyboard layout with modifiers encoded in upper byte
    for(char c = 'a'; c <= 'z'; c++) {
        app->layout[(unsigned char)c] = HID_KEYBOARD_A + (c - 'a');
    }
    for(char c = 'A'; c <= 'Z'; c++) {
        app->layout[(unsigned char)c] = (KEY_MOD_LEFT_SHIFT << 8) | (HID_KEYBOARD_A + (c - 'A'));
    }
    for(char c = '1'; c <= '9'; c++) {
        app->layout[(unsigned char)c] = HID_KEYBOARD_1 + (c - '1');
    }
    app->layout['0'] = HID_KEYBOARD_0;
    app->layout[' '] = HID_KEYBOARD_SPACEBAR;
    app->layout['\n'] = HID_KEYBOARD_RETURN;
    app->layout['\r'] = HID_KEYBOARD_RETURN;
    app->layout['\t'] = HID_KEYBOARD_TAB;
    
    // Symbols with shift
    app->layout['!'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_1;
    app->layout['@'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_2;
    app->layout['#'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_3;
    app->layout['$'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_4;
    app->layout['%'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_5;
    app->layout['^'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_6;
    app->layout['&'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_7;
    app->layout['*'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_8;
    app->layout['('] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_9;
    app->layout[')'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_0;
    
    app->layout['-'] = HID_KEYBOARD_MINUS;
    app->layout['_'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_MINUS;
    app->layout['='] = HID_KEYBOARD_EQUAL_SIGN;
    app->layout['+'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_EQUAL_SIGN;
    app->layout['['] = HID_KEYBOARD_OPEN_BRACKET;
    app->layout['{'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_OPEN_BRACKET;
    app->layout[']'] = HID_KEYBOARD_CLOSE_BRACKET;
    app->layout['}'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_CLOSE_BRACKET;
    app->layout['\\'] = HID_KEYBOARD_BACKSLASH;
    app->layout['|'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_BACKSLASH;
    app->layout[';'] = HID_KEYBOARD_SEMICOLON;
    app->layout[':'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_SEMICOLON;
    app->layout['\''] = HID_KEYBOARD_APOSTROPHE;
    app->layout['"'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_APOSTROPHE;
    app->layout['`'] = HID_KEYBOARD_GRAVE_ACCENT;
    app->layout['~'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_GRAVE_ACCENT;
    app->layout[','] = HID_KEYBOARD_COMMA;
    app->layout['<'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_COMMA;
    app->layout['.'] = HID_KEYBOARD_DOT;
    app->layout['>'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_DOT;
    app->layout['/'] = HID_KEYBOARD_SLASH;
    app->layout['?'] = (KEY_MOD_LEFT_SHIFT << 8) | HID_KEYBOARD_SLASH;
    
    app->layout_loaded = true;
    
    if(app->keyboard_layout[0] != '\0') {
        char layout_path[96];
        snprintf(layout_path, sizeof(layout_path), "%s/%s", BADUSB_LAYOUTS_DIR, app->keyboard_layout);
        
        Storage* storage = furi_record_open(RECORD_STORAGE);
        File* file = storage_file_alloc(storage);
        
        if(storage_file_open(file, layout_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            storage_file_read(file, app->layout, sizeof(app->layout));
            storage_file_close(file);
        }
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
    }
}

static void uid_to_hex(const uint8_t* uid, size_t uid_len, char* hex_out) {
    for(size_t i = 0; i < uid_len; i++) {
        snprintf(hex_out + i * 2, 3, "%02X", uid[i]);
    }
    hex_out[uid_len * 2] = '\0';
}

static void app_ensure_data_dir(Storage* storage) {
    storage_common_mkdir(storage, APP_DATA_DIR);
}

static bool ensure_crypto_key(void) {
    if(furi_hal_crypto_enclave_ensure_key(CRYPTO_KEY_SLOT)) {
        return true;
    }
    FURI_LOG_E(TAG, "ensure_crypto_key: Failed to ensure crypto key slot %d", CRYPTO_KEY_SLOT);
    return false;
}

// Generate IV from device UID for consistency (same device = same IV)
static void generate_iv_from_device_uid(uint8_t* iv) {
    const uint8_t* device_uid = furi_hal_version_uid();
    size_t uid_size = furi_hal_version_uid_size();
    
    memset(iv, 0, AES_BLOCK_SIZE);
    if(uid_size > 0) {
        size_t copy_size = uid_size < AES_BLOCK_SIZE ? uid_size : AES_BLOCK_SIZE;
        memcpy(iv, device_uid, copy_size);
        // Fill remainder with device UID repeated if needed
        for(size_t i = copy_size; i < AES_BLOCK_SIZE; i++) {
            iv[i] = device_uid[i % uid_size];
        }
    }
}

// Encrypt data using AES-CBC with key from secure enclave
// Encrypt data - PURE CRYPTO FUNCTION, NO STORAGE OPERATIONS
// Returns encrypted data in output buffer, output_len set to encrypted size
static bool encrypt_data(const uint8_t* input, size_t input_len, uint8_t* output, size_t* output_len) {
    if(!input || !output || !output_len || input_len == 0) {
        FURI_LOG_E(TAG, "encrypt_data: Invalid parameters");
        return false;
    }
    
    if(input_len > MAX_ENCRYPTED_SIZE) {
        FURI_LOG_E(TAG, "encrypt_data: Input too large: %zu", input_len);
        return false;
    }
    
    // Generate IV from device UID
    uint8_t iv[AES_BLOCK_SIZE];
    generate_iv_from_device_uid(iv);
    
    // Calculate padded length (AES requires 16-byte blocks)
    size_t padded_len = ((input_len + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE) * AES_BLOCK_SIZE;
    if(padded_len > MAX_ENCRYPTED_SIZE) {
        FURI_LOG_E(TAG, "encrypt_data: Padded length too large: %zu", padded_len);
        return false;
    }
    
    // Allocate and pad input
    uint8_t* padded_input = malloc(padded_len);
    if(!padded_input) {
        FURI_LOG_E(TAG, "encrypt_data: Failed to allocate padded buffer");
        return false;
    }
    
    memcpy(padded_input, input, input_len);
    uint8_t pad_value = padded_len - input_len;
    memset(padded_input + input_len, pad_value, pad_value);
    
    // Load key and encrypt
    bool success = false;
    if(furi_hal_crypto_enclave_load_key(CRYPTO_KEY_SLOT, iv)) {
        success = furi_hal_crypto_encrypt(padded_input, output, padded_len);
        furi_hal_crypto_enclave_unload_key(CRYPTO_KEY_SLOT);
        
        if(success) {
            *output_len = padded_len;
        } else {
            FURI_LOG_E(TAG, "encrypt_data: Encryption failed");
        }
    } else {
        FURI_LOG_E(TAG, "encrypt_data: Failed to load key from enclave");
    }
    
    // Cleanup
    memset(padded_input, 0, padded_len);
    free(padded_input);
    
    return success;
}

// Decrypt data - PURE CRYPTO FUNCTION, NO STORAGE OPERATIONS
// Returns decrypted data in output buffer, output_len set to decrypted size
static bool decrypt_data(const uint8_t* input, size_t input_len, uint8_t* output, size_t* output_len) {
    if(!input || !output || !output_len) {
        FURI_LOG_E(TAG, "decrypt_data: Invalid parameters");
        return false;
    }
    
    if(input_len == 0 || input_len % AES_BLOCK_SIZE != 0) {
        FURI_LOG_E(TAG, "decrypt_data: Invalid input length: %zu", input_len);
        return false;
    }
    
    if(input_len > MAX_ENCRYPTED_SIZE) {
        FURI_LOG_E(TAG, "decrypt_data: Input too large: %zu", input_len);
        return false;
    }
    
    // Generate IV from device UID (must match encryption)
    uint8_t iv[AES_BLOCK_SIZE];
    generate_iv_from_device_uid(iv);
    
    // Load key and decrypt
    bool success = false;
    if(furi_hal_crypto_enclave_load_key(CRYPTO_KEY_SLOT, iv)) {
        success = furi_hal_crypto_decrypt(input, output, input_len);
        furi_hal_crypto_enclave_unload_key(CRYPTO_KEY_SLOT);
        
        if(success) {
            // Remove padding
            uint8_t pad_value = output[input_len - 1];
            if(pad_value > 0 && pad_value <= AES_BLOCK_SIZE && pad_value <= input_len) {
                *output_len = input_len - pad_value;
            } else {
                *output_len = input_len;
            }
        } else {
            FURI_LOG_E(TAG, "decrypt_data: Decryption failed");
        }
    } else {
        FURI_LOG_E(TAG, "decrypt_data: Failed to load key from enclave");
    }
    
    return success;
}


// Helper function to render edit menu
static void app_render_edit_menu(App* app) {
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Edit Card");
    const char* items[] = {"Name", "Password", "UID", "Delete"};
    for(size_t i = 0; i < 4; i++) {
        char line[32];
        snprintf(line, sizeof(line), "%s %s", (i == app->edit_menu_index) ? ">" : " ", items[i]);
        widget_add_string_element(app->widget, 0, 12 + i * 12, AlignLeft, AlignTop, FontSecondary, line);
    }
}

// Helper function to render card list
static void app_render_card_list(App* app) {
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Cards");
    if(app->card_count == 0) {
        widget_add_string_element(app->widget, 0, 20, AlignLeft, AlignTop, FontSecondary, "No cards stored");
    } else {
        // Show visible items based on scroll offset
        for(uint8_t i = 0; i < CARD_LIST_VISIBLE_ITEMS; i++) {
            size_t card_index = app->card_list_scroll_offset + i;
            if(card_index < app->card_count) {
                char line[64];
                // Show ">" for currently navigated card, "*" for active/selected card
                const char* nav_marker = (card_index == app->selected_card) ? ">" : " ";
                const char* active_marker = (app->has_active_selection && card_index == app->active_card_index) ? "*" : " ";
                snprintf(line, sizeof(line), "%s%s %zu. %s", 
                        nav_marker, active_marker, card_index + 1, app->cards[card_index].name);
                widget_add_string_element(app->widget, 0, 12 + i * 10, AlignLeft, AlignTop, FontSecondary, line);
            }
        }
    }
    widget_add_string_element(app->widget, 0, 54, AlignLeft, AlignTop, FontSecondary, "OK=Sel  Hold OK=Edit  >Import");
}

// Render credits page
static void app_render_credits(App* app) {
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Credits");
    
    // Page 0: Main credits
    if(app->credits_page == 0) {
        widget_add_string_element(app->widget, 0, 12, AlignLeft, AlignTop, FontSecondary, "NFC Login");
        widget_add_string_element(app->widget, 0, 22, AlignLeft, AlignTop, FontSecondary, "Version: 1.0");
        widget_add_string_element(app->widget, 0, 32, AlignLeft, AlignTop, FontSecondary, "Creator: Play2BReal");
        widget_add_string_element(app->widget, 0, 42, AlignLeft, AlignTop, FontSecondary, "github.com/Play2BReal");
    }
    // Page 1: Contributors/Thanks
    else if(app->credits_page == 1) {
        widget_add_string_element(app->widget, 0, 12, AlignLeft, AlignTop, FontSecondary, "Special Thanks To:");
        widget_add_string_element(app->widget, 0, 22, AlignLeft, AlignTop, FontSecondary, "Equip, Tac0s, WillyJL");
        widget_add_string_element(app->widget, 0, 32, AlignLeft, AlignTop, FontSecondary, "& The Biohacking Community!");
        widget_add_string_element(app->widget, 0, 42, AlignLeft, AlignTop, FontSecondary, "");
    }
    
    char page_info[32];
    snprintf(page_info, sizeof(page_info), "Page %d/%d  <- ->=Navigate", app->credits_page + 1, CREDITS_PAGES);
    widget_add_string_element(app->widget, 0, SETTINGS_HELP_Y_POS, AlignLeft, AlignTop, FontSecondary, page_info);
}

// Render settings view
static void app_render_settings(App* app) {
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Settings");
    
    // Show 3 items at a time based on scroll offset
    char setting_lines[SETTINGS_MENU_ITEMS][64];
    char layout_display[32];
    strncpy(layout_display, app->keyboard_layout, sizeof(layout_display) - 1);
    layout_display[sizeof(layout_display) - 1] = '\0';
    
    snprintf(setting_lines[0], sizeof(setting_lines[0]), "%s Append Enter: %s", 
            (app->settings_menu_index == 0) ? ">" : " ", app->append_enter ? "ON" : "OFF");
    snprintf(setting_lines[1], sizeof(setting_lines[1]), "%s Input Delay: %dms", 
            (app->settings_menu_index == 1) ? ">" : " ", app->input_delay_ms);
    snprintf(setting_lines[2], sizeof(setting_lines[2]), "%s Keyboard Layout: %s", 
            (app->settings_menu_index == 2) ? ">" : " ", layout_display);
    snprintf(setting_lines[3], sizeof(setting_lines[3]), "%s Credits", 
            (app->settings_menu_index == 3) ? ">" : " ");
    
    // Display visible items (3 at a time)
    for(uint8_t i = 0; i < SETTINGS_VISIBLE_ITEMS; i++) {
        uint8_t item_index = app->settings_scroll_offset + i;
        if(item_index < SETTINGS_MENU_ITEMS) {
            widget_add_string_element(app->widget, 0, 12 + i * 12, AlignLeft, AlignTop, FontSecondary, setting_lines[item_index]);
        }
    }
    
    // Help text at fixed position
    if(app->settings_menu_index == 0) {
        widget_add_string_element(app->widget, 0, SETTINGS_HELP_Y_POS, AlignLeft, AlignTop, FontSecondary, "OK=Toggle  Back=Menu");
    } else if(app->settings_menu_index == 1) {
        widget_add_string_element(app->widget, 0, SETTINGS_HELP_Y_POS, AlignLeft, AlignTop, FontSecondary, "<-> Cycle  Back=Menu");
    } else if(app->settings_menu_index == 2) {
        widget_add_string_element(app->widget, 0, SETTINGS_HELP_Y_POS, AlignLeft, AlignTop, FontSecondary, "<-> Cycle  OK=Sel  Back=Menu");
    } else if(app->settings_menu_index == 3) {
        widget_add_string_element(app->widget, 0, SETTINGS_HELP_Y_POS, AlignLeft, AlignTop, FontSecondary, "OK=View  Back=Menu");
    }
}

// Type password using layout array (modifiers encoded in upper byte)
static uint32_t app_type_password(App* app, const char* password) {
    if(!password) return 0;
    
    if(!app->layout_loaded) {
        app_load_keyboard_layout(app);
    }
    
    uint32_t approx_typed_ms = 0;
    
    for(size_t i = 0; password[i] != '\0'; i++) {
        unsigned char uc = (unsigned char)password[i];
        
        if(uc >= 128) continue;
        
        uint16_t full_keycode = app->layout[uc];
        
        if(full_keycode != HID_KEYBOARD_NONE) {
            furi_hal_hid_kb_press(full_keycode);
            furi_delay_ms(KEY_PRESS_DELAY_MS);
            furi_hal_hid_kb_release(full_keycode);
            furi_delay_ms(KEY_RELEASE_DELAY_MS);
            approx_typed_ms += KEY_PRESS_DELAY_MS + KEY_RELEASE_DELAY_MS;
            
            uint16_t delay = app ? app->input_delay_ms : KEY_RELEASE_DELAY_MS;
            furi_delay_ms(delay);
            approx_typed_ms += delay;
        }
    }
    
    if(app && app->append_enter) {
        furi_hal_hid_kb_press(HID_KEYBOARD_RETURN);
        furi_delay_ms(ENTER_PRESS_DELAY_MS);
        furi_hal_hid_kb_release(HID_KEYBOARD_RETURN);
        furi_delay_ms(ENTER_RELEASE_DELAY_MS);
        approx_typed_ms += ENTER_PRESS_DELAY_MS + ENTER_RELEASE_DELAY_MS;
    }
    
    return approx_typed_ms;
}

// NFC scanning thread
static int32_t app_scan_thread(void* context) {
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
                        notification_message(app->notification, &sequence_success);
                        // Save current USB config
                        app->previous_usb_config = furi_hal_usb_get_config();
                    // Initialize HID and wait for connection
                    if(initialize_hid_and_wait()) {
                        // Fixed delay after HID connect before typing
                        furi_delay_ms(HID_POST_CONNECT_DELAY_MS);
                        // Ensure no stuck modifiers
                        furi_hal_hid_kb_release_all();
                        // Type the password and wait proportionally to string length
                        uint32_t typed_ms = app_type_password(app, app->cards[match_index].password);
                        // Additional delay equal to approximate typing time
                        if(typed_ms > 0) {
                            furi_delay_ms(typed_ms);
                        }
                    } else {
                        notification_message(app->notification, &sequence_error);
                    }
                    // Restore previous USB config regardless
                        deinitialize_hid_with_restore(app->previous_usb_config);
                        app->previous_usb_config = NULL;
                        // Wait a bit before scanning again
                        furi_delay_ms(HID_POST_TYPE_DELAY_MS);
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

// Settings functions
static void app_save_settings(App* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    
    app_ensure_data_dir(storage);
    
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, NFC_SETTINGS_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char line[128];
        snprintf(line, sizeof(line), "append_enter=%d\n", app->append_enter ? 1 : 0);
        storage_file_write(file, line, strlen(line));
        snprintf(line, sizeof(line), "input_delay=%d\n", app->input_delay_ms);
        storage_file_write(file, line, strlen(line));
        snprintf(line, sizeof(line), "keyboard_layout=%s\n", app->keyboard_layout);
        storage_file_write(file, line, strlen(line));
        // Save last selected card index if there's an active selection
        if(app->has_active_selection && app->active_card_index < app->card_count) {
            snprintf(line, sizeof(line), "active_card_index=%zu\n", app->active_card_index);
            storage_file_write(file, line, strlen(line));
        }
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    
    app_load_keyboard_layout(app);
}

static void app_load_settings(App* app) {
    // Defaults
    app->append_enter = true;
    app->input_delay_ms = 10;
    strncpy(app->keyboard_layout, "en-US.kl", sizeof(app->keyboard_layout) - 1);
    app->keyboard_layout[sizeof(app->keyboard_layout) - 1] = '\0';
    app->selecting_keyboard_layout = false;
    app->layout_loaded = false;
    app->has_active_selection = false;
    app->active_card_index = 0;
    
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    
    if(storage_file_open(file, NFC_SETTINGS_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[128];
        size_t len = 0;
        uint8_t ch = 0;
        
        while(true) {
            len = 0;
            while(len < sizeof(line) - 1) {
                uint16_t rd = storage_file_read(file, &ch, 1);
                if(rd == 0 || ch == '\n') break;
                line[len++] = (char)ch;
            }
            if(len == 0) break;
            line[len] = '\0';
            
            if(strncmp(line, "append_enter=", 13) == 0) {
                int value = atoi(line + 13);
                app->append_enter = (value != 0);
            }
            else if(strncmp(line, "input_delay=", 12) == 0) {
                int value = atoi(line + 12);
                if(value == 10 || value == 50 || value == 100 || value == 200) {
                    app->input_delay_ms = (uint16_t)value;
                }
            }
            else if(strncmp(line, "keyboard_delay=", 15) == 0) {
                int value = atoi(line + 15);
                if(value == 0 || value == 10) {
                    app->input_delay_ms = 10;
                } else if(value == 50) {
                    app->input_delay_ms = 50;
                } else if(value == 100) {
                    app->input_delay_ms = 100;
                } else if(value >= 200) {
                    app->input_delay_ms = 200;
                }
            }
            else if(strncmp(line, "keyboard_layout=", 17) == 0) {
                const char* layout_name = line + 17;
                size_t name_len = strlen(layout_name);
                if(name_len > 0 && name_len < sizeof(app->keyboard_layout)) {
                    strncpy(app->keyboard_layout, layout_name, sizeof(app->keyboard_layout) - 1);
                    app->keyboard_layout[sizeof(app->keyboard_layout) - 1] = '\0';
                    
                    char* newline = strchr(app->keyboard_layout, '\n');
                    if(newline) *newline = '\0';
                    newline = strchr(app->keyboard_layout, '\r');
                    if(newline) *newline = '\0';
                } else if(name_len == 0) {
                    strncpy(app->keyboard_layout, "en-US.kl", sizeof(app->keyboard_layout) - 1);
                    app->keyboard_layout[sizeof(app->keyboard_layout) - 1] = '\0';
                }
            }
            else if(strncmp(line, "active_card_index=", 18) == 0) {
                size_t index = (size_t)atoi(line + 18);
                // Will be validated after cards are loaded
                app->active_card_index = index;
            }
            
            if(storage_file_tell(file) == storage_file_size(file)) break;
        }
        
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    
    // Load keyboard layout after settings are loaded
    app_load_keyboard_layout(app);
}

// Storage functions
// Save cards - COMPLETE SEPARATION: crypto first, then file I/O
static bool app_save_cards(App* app) {
    FURI_LOG_I(TAG, "app_save_cards: Starting save for %zu cards", app->card_count);
    
    // PHASE 1: Build plaintext data (no storage, no crypto)
    size_t estimated_size = app->card_count * 256;
    if(estimated_size < 512) estimated_size = 512;
        if(estimated_size > MAX_ENCRYPTED_SIZE) estimated_size = MAX_ENCRYPTED_SIZE;
    
    char* plaintext = malloc(estimated_size);
    if(!plaintext) {
        FURI_LOG_E(TAG, "app_save_cards: Failed to allocate plaintext buffer");
        return false;
    }
    
    size_t plaintext_len = 0;
    for(size_t i = 0; i < app->card_count; i++) {
        char line[128];
        char uid_hex[MAX_UID_LEN * 2 + 1] = {0};
        
        uid_to_hex(app->cards[i].uid, app->cards[i].uid_len, uid_hex);
        
        int written = snprintf(line, sizeof(line), "%s|%s|%s\n", 
                app->cards[i].name, uid_hex, app->cards[i].password);
        if(written > 0 && plaintext_len + written < estimated_size) {
            memcpy(plaintext + plaintext_len, line, written);
            plaintext_len += written;
        }
    }
    FURI_LOG_D(TAG, "app_save_cards: Built plaintext, length=%zu", plaintext_len);
    
    // PHASE 2: Encrypt data (PURE CRYPTO, NO STORAGE)
    uint8_t* encrypted = NULL;
    size_t encrypted_len = 0;
    bool encryption_success = false;
    
    if(plaintext_len > 0) {
        encrypted = malloc(estimated_size + AES_BLOCK_SIZE);
        if(!encrypted) {
            FURI_LOG_E(TAG, "app_save_cards: Failed to allocate encrypted buffer");
            memset(plaintext, 0, estimated_size);
            free(plaintext);
            return false;
        }
        
        FURI_LOG_D(TAG, "app_save_cards: Encrypting %zu bytes", plaintext_len);
        encryption_success = encrypt_data((uint8_t*)plaintext, plaintext_len, encrypted, &encrypted_len);
        
        if(!encryption_success) {
            FURI_LOG_E(TAG, "app_save_cards: Encryption failed");
            memset(plaintext, 0, estimated_size);
            memset(encrypted, 0, estimated_size + AES_BLOCK_SIZE);
            free(plaintext);
            free(encrypted);
            return false;
        }
        
        FURI_LOG_D(TAG, "app_save_cards: Encryption successful, encrypted_len=%zu", encrypted_len);
        
        // Cleanup plaintext immediately after encryption
        memset(plaintext, 0, estimated_size);
        free(plaintext);
        plaintext = NULL;
        
        // Wait for crypto subsystem to settle
        furi_delay_ms(CRYPTO_SETTLE_DELAY_MS);
    } else if(app->card_count == 0) {
        // Empty file case - no encryption needed
        memset(plaintext, 0, estimated_size);
        free(plaintext);
        plaintext = NULL;
    }
    
    // PHASE 3: File I/O (FRESH STORAGE HANDLE, NO CRYPTO)
    bool success = false;
    
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        FURI_LOG_E(TAG, "app_save_cards: Failed to open storage record");
        if(encrypted) {
            memset(encrypted, 0, encrypted_len);
            free(encrypted);
        }
        return false;
    }
    
    // Check SD status
    FS_Error sd_status = storage_sd_status(storage);
    if(sd_status != FSE_OK) {
        FURI_LOG_E(TAG, "app_save_cards: SD not ready (status=%d)", sd_status);
        furi_record_close(RECORD_STORAGE);
        if(encrypted) {
            memset(encrypted, 0, encrypted_len);
            free(encrypted);
        }
        return false;
    }
    
    // Ensure directory exists
    app_ensure_data_dir(storage);
    
    // Allocate file object
    File* file = storage_file_alloc(storage);
    if(!file) {
        FURI_LOG_E(TAG, "app_save_cards: Failed to allocate file object");
        furi_record_close(RECORD_STORAGE);
        if(encrypted) {
            memset(encrypted, 0, encrypted_len);
            free(encrypted);
        }
        return false;
    }
    
    // Open file
    bool file_opened = storage_file_open(file, NFC_CARDS_FILE_ENC, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(!file_opened) {
        FURI_LOG_E(TAG, "app_save_cards: Failed to open file for writing");
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        if(encrypted) {
            memset(encrypted, 0, encrypted_len);
            free(encrypted);
        }
        return false;
    }
    
    // Write data
    if(encryption_success && encrypted_len > 0) {
        FURI_LOG_D(TAG, "app_save_cards: Writing %zu encrypted bytes", encrypted_len);
        size_t written = storage_file_write(file, encrypted, encrypted_len);
        if(written == encrypted_len) {
            success = true;
            FURI_LOG_I(TAG, "app_save_cards: Successfully saved %zu cards (encrypted)", app->card_count);
        } else {
            FURI_LOG_E(TAG, "app_save_cards: Write failed: %zu/%zu bytes", written, encrypted_len);
        }
    } else if(app->card_count == 0) {
        // Empty file - just create it
        success = true;
        FURI_LOG_I(TAG, "app_save_cards: Created empty encrypted file");
    }
    
    // Close file and storage
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    
    // Cleanup encrypted data
    if(encrypted) {
        memset(encrypted, 0, encrypted_len);
        free(encrypted);
    }
    
    return success;
}

// Helper function to parse a line and add card
static bool parse_card_line(const char* line, NfcCard* card) {
    char line_copy[128];
    strncpy(line_copy, line, sizeof(line_copy) - 1);
    line_copy[sizeof(line_copy) - 1] = '\0';
    
    // Parse: name|uid_hex|password
    char* name = strtok(line_copy, "|");
    char* uid_hex = strtok(NULL, "|");
    char* password = strtok(NULL, "");
    
    if(password) {
        // Strip any trailing newline leftovers/spaces
        for(char* p = password; *p; ++p) {
            if(*p == '\r' || *p == '\n') {
                *p = '\0';
                break;
            }
        }
    }
    
    if(name && uid_hex && password) {
        memset(card, 0, sizeof(NfcCard));
        strncpy(card->name, name, sizeof(card->name) - 1);
        strncpy(card->password, password, sizeof(card->password) - 1);
        
        // Parse hex UID
        size_t uid_len = strlen(uid_hex) / 2;
        if(uid_len > 0 && uid_len <= MAX_UID_LEN) {
            card->uid_len = uid_len;
            for(size_t i = 0; i < uid_len; i++) {
                unsigned int byte_val = 0;
                sscanf(uid_hex + i * 2, "%2x", &byte_val);
                card->uid[i] = (uint8_t)byte_val;
            }
            return true;
        }
    }
    return false;
}

// Load cards - COMPLETE SEPARATION: file I/O first, then crypto
static void app_load_cards(App* app) {
    app->card_count = 0;
    
    // PHASE 1: File I/O - Read encrypted data (NO CRYPTO YET)
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        FURI_LOG_E(TAG, "app_load_cards: Failed to open storage record");
        return;
    }
    
    File* file = storage_file_alloc(storage);
    if(!file) {
        FURI_LOG_E(TAG, "app_load_cards: Failed to allocate file object");
        furi_record_close(RECORD_STORAGE);
        return;
    }
    
    uint8_t* encrypted = NULL;
    size_t encrypted_len = 0;
    bool file_read_success = false;
    
    // Try encrypted file first
    if(storage_file_open(file, NFC_CARDS_FILE_ENC, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t file_size = storage_file_size(file);
        
        if(file_size > MAX_ENCRYPTED_SIZE) {
            FURI_LOG_E(TAG, "app_load_cards: Encrypted file too large: %zu", file_size);
            storage_file_close(file);
            storage_file_free(file);
            furi_record_close(RECORD_STORAGE);
            return;
        }
        
        if(file_size % AES_BLOCK_SIZE != 0) {
            FURI_LOG_E(TAG, "app_load_cards: Invalid encrypted file size: %zu", file_size);
            storage_file_close(file);
            storage_file_free(file);
            furi_record_close(RECORD_STORAGE);
            return;
        }
        
        encrypted = malloc(file_size);
        if(!encrypted) {
            FURI_LOG_E(TAG, "app_load_cards: Failed to allocate encrypted buffer");
            storage_file_close(file);
            storage_file_free(file);
            furi_record_close(RECORD_STORAGE);
            return;
        }
        
        size_t bytes_read = 0;
        while(bytes_read < file_size) {
            uint16_t rd = storage_file_read(file, encrypted + bytes_read, file_size - bytes_read);
            if(rd == 0) break;
            bytes_read += rd;
        }
        
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        
        if(bytes_read == file_size) {
            encrypted_len = bytes_read;
            file_read_success = true;
            FURI_LOG_D(TAG, "app_load_cards: Read %zu encrypted bytes", encrypted_len);
        } else {
            FURI_LOG_E(TAG, "app_load_cards: Read incomplete: %zu/%zu", bytes_read, file_size);
            memset(encrypted, 0, file_size);
            free(encrypted);
            encrypted = NULL;
        }
    } else {
        // No encrypted file yet: treat as empty (allow first save to create it)
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        app->card_count = 0;
        return;
    }
    
    // PHASE 2: Decrypt data (PURE CRYPTO, NO STORAGE)
    if(file_read_success && encrypted_len > 0) {
        if(!ensure_crypto_key()) {
            FURI_LOG_E(TAG, "app_load_cards: Failed to ensure crypto key");
            memset(encrypted, 0, encrypted_len);
            free(encrypted);
            return;
        }
        
        char* plaintext = malloc(MAX_ENCRYPTED_SIZE);
        if(!plaintext) {
            FURI_LOG_E(TAG, "app_load_cards: Failed to allocate plaintext buffer");
            memset(encrypted, 0, encrypted_len);
            free(encrypted);
            return;
        }
        
        size_t plaintext_len = 0;
        FURI_LOG_D(TAG, "app_load_cards: Decrypting %zu bytes", encrypted_len);
        bool decrypt_success = decrypt_data(encrypted, encrypted_len, (uint8_t*)plaintext, &plaintext_len);
        
        memset(encrypted, 0, encrypted_len);
        free(encrypted);
        encrypted = NULL;
        
        if(!decrypt_success) {
            FURI_LOG_E(TAG, "app_load_cards: Decryption failed");
            memset(plaintext, 0, MAX_ENCRYPTED_SIZE);
            free(plaintext);
            return;
        }
        
        FURI_LOG_D(TAG, "app_load_cards: Decryption successful, plaintext_len=%zu", plaintext_len);
        furi_delay_ms(STORAGE_READ_DELAY_MS);
        
        // PHASE 3: Parse plaintext (NO CRYPTO, NO STORAGE)
        char* line_start = plaintext;
        while(app->card_count < MAX_CARDS && line_start < plaintext + plaintext_len) {
            char* line_end = strchr(line_start, '\n');
            if(!line_end) {
                line_end = plaintext + plaintext_len;
            }
            
            size_t line_len = line_end - line_start;
            if(line_len > 0 && line_len < 256) {
                char line[256];
                memcpy(line, line_start, line_len);
                line[line_len] = '\0';
                
                if(line_len > 0 && line[line_len - 1] == '\r') {
                    line[line_len - 1] = '\0';
                }
                
                if(line[0] != '\0') {
                    if(parse_card_line(line, &app->cards[app->card_count])) {
                        app->card_count++;
                    }
                }
            }
            
            if(line_end >= plaintext + plaintext_len) break;
            line_start = line_end + 1;
        }
        
        memset(plaintext, 0, 4096);
        free(plaintext);
        
        FURI_LOG_I(TAG, "app_load_cards: Loaded %zu cards", app->card_count);
    }
    
    // Validate and restore last selected card if it was saved
    if(app->active_card_index < app->card_count && app->card_count > 0) {
        app->has_active_selection = true;
        app->selected_card = app->active_card_index;
        FURI_LOG_I(TAG, "app_load_cards: Restored active card index %zu", app->active_card_index);
    } else if(app->active_card_index > 0) {
        // Index was saved but card no longer exists - clear selection
        app->has_active_selection = false;
        app->active_card_index = 0;
        FURI_LOG_W(TAG, "app_load_cards: Saved card index no longer exists (card_count=%zu), clearing selection", app->card_count);
    }
}

// Submenu callbacks
static void submenu_callback(void* context, uint32_t index) {
    App* app = context;
    
    switch(index) {
    case SubmenuAddCard:
        // Defer view switch until after current key event completes
        view_dispatcher_send_custom_event(app->view_dispatcher, EventAddCardStart);
        break;
    case SubmenuListCards:
        // Show list of cards
        app->widget_state = 2; // list mode
        app->selected_card = 0;
        app->card_list_scroll_offset = 0;
        app_render_card_list(app);
        app_switch_to_view(app, ViewWidget);
        break;
    case SubmenuStartScan:
        if(!app->scanning) {
            app->scanning = true;
            app->scan_thread = furi_thread_alloc();
            furi_thread_set_name(app->scan_thread, "NfcScan");
            furi_thread_set_stack_size(app->scan_thread, 4 * 1024);
            furi_thread_set_context(app->scan_thread, app);
            furi_thread_set_callback(app->scan_thread, app_scan_thread);
            furi_thread_start(app->scan_thread);
            
            widget_reset(app->widget);
            app->widget_state = 1; // scan mode
            // Text-based scan UI for login
            widget_add_string_element(app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Scanning for NFC...");
            widget_add_string_element(app->widget, 0, 20, AlignLeft, AlignTop, FontSecondary, "Hold card to reader");
            widget_add_string_element(app->widget, 0, 40, AlignLeft, AlignTop, FontSecondary, "Press Back to stop");
            app_switch_to_view(app, ViewWidget);
        }
        break;
    case SubmenuSettings:
        // Show settings view
        app->widget_state = 4; // settings mode
        app->settings_menu_index = 0; // Reset to first setting
        app->settings_scroll_offset = 0; // Reset scroll to show first 3 items
        app->selecting_keyboard_layout = false;
        app_render_settings(app);
        app_switch_to_view(app, ViewWidget);
        break;
    }
}

// Text input result callback
static void app_text_input_result_callback(void* context) {
    App* app = context;
    
    if(app->enrollment_state == EnrollmentStateName) {
        // Name entered, now scan for UID
        if(strlen(app->enrollment_card.name) > 0) {
            view_dispatcher_send_custom_event(app->view_dispatcher, EventStartScan);
        } else {
            app->enrollment_state = EnrollmentStateNone;
            view_dispatcher_switch_to_view(app->view_dispatcher, ViewSubmenu);
        }
    } else if(app->enrollment_state == EnrollmentStatePassword) {
        // Password entered, save the card
        FURI_LOG_I(TAG, "app_text_input_result_callback: Password entered, password_len=%zu, uid_len=%zu", 
                   strlen(app->enrollment_card.password), app->enrollment_card.uid_len);
        if(strlen(app->enrollment_card.password) > 0 && app->enrollment_card.uid_len > 0) {
            if(app->card_count < MAX_CARDS) {
                FURI_LOG_D(TAG, "app_text_input_result_callback: Adding card %zu to array", app->card_count);
                memcpy(&app->cards[app->card_count], &app->enrollment_card, sizeof(NfcCard));
                app->card_count++;
                FURI_LOG_D(TAG, "app_text_input_result_callback: Card added, calling app_save_cards");
                if(app_save_cards(app)) {
                    FURI_LOG_I(TAG, "app_text_input_result_callback: Save successful");
                    notification_message(app->notification, &sequence_success);
                } else {
                    // Save failed - remove the card we just added and show error
                    FURI_LOG_E(TAG, "app_text_input_result_callback: Save failed, removing card");
                    app->card_count--;
                    notification_message(app->notification, &sequence_error);
                }
            } else {
                FURI_LOG_E(TAG, "app_text_input_result_callback: Max cards reached (%d)", MAX_CARDS);
                notification_message(app->notification, &sequence_error);
            }
        } else {
            FURI_LOG_E(TAG, "app_text_input_result_callback: Invalid password or UID (password_len=%zu, uid_len=%zu)", 
                       strlen(app->enrollment_card.password), app->enrollment_card.uid_len);
        }
        app->enrollment_state = EnrollmentStateNone;
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewSubmenu);
    }
}

static bool app_widget_view_input(InputEvent* event, void* context) {
    App* app = context;
    if(event->type == InputTypeShort) {
        if(event->key == InputKeyBack) {
        if(app->scanning) {
                // Stop scanning thread
            app->scanning = false;
            if(app->scan_thread) {
                furi_thread_join(app->scan_thread);
                furi_thread_free(app->scan_thread);
                app->scan_thread = NULL;
            }
            } else if(app->enrollment_scanning) {
                // Stop enrollment scanning
                app->enrollment_scanning = false;
                if(app->enroll_scan_thread) {
                    furi_thread_join(app->enroll_scan_thread);
                    furi_thread_free(app->enroll_scan_thread);
                    app->enroll_scan_thread = NULL;
                }
                app->widget_state = 0;
            } else if(app->enrollment_state != EnrollmentStateNone) {
                // Cancel enrollment if we're in the middle of adding a card
                app->enrollment_state = EnrollmentStateNone;
            } else if(app->widget_state == 4) {
                // Settings view: return to submenu
                app->widget_state = 0;
            }
            app_switch_to_view(app, ViewSubmenu);
        return true;
        }
        // Settings view: navigation and changes
        if(app->widget_state == 4) {
            if(event->key == InputKeyUp) {
                if(app->settings_menu_index > 0) {
                    app->settings_menu_index--;
                    // Update scroll offset to keep selected item visible
                    if(app->settings_menu_index < app->settings_scroll_offset) {
                        app->settings_scroll_offset = app->settings_menu_index;
                    }
                }
            } else if(event->key == InputKeyDown) {
                if(app->settings_menu_index < SETTINGS_MENU_ITEMS - 1) {
                    app->settings_menu_index++;
                    // Update scroll offset to keep selected item visible
                    if(app->settings_menu_index >= app->settings_scroll_offset + SETTINGS_VISIBLE_ITEMS) {
                        app->settings_scroll_offset = app->settings_menu_index - (SETTINGS_VISIBLE_ITEMS - 1);
                    }
                }
            } else if(event->key == InputKeyOk) {
                if(app->settings_menu_index == 0) {
                    // Toggle append enter
                    app->append_enter = !app->append_enter;
                    app_save_settings(app);
                    notification_message(app->notification, &sequence_success);
                } else if(app->settings_menu_index == 2) {
                    app->selecting_keyboard_layout = true;
                    furi_string_set(app->fb_output_path, BADUSB_LAYOUTS_DIR);
                    file_browser_configure(
                        app->file_browser,
                        ".kl",
                        BADUSB_LAYOUTS_DIR,
                        true,
                        true,
                        NULL,
                        false
                    );
                    file_browser_start(app->file_browser, app->fb_output_path);
                    app_switch_to_view(app, ViewFileBrowser);
                    return true;
                } else if(app->settings_menu_index == 3) {
                    // Open Credits view
                    app->widget_state = 5;
                    app->credits_page = 0;
                    widget_reset(app->widget);
                    app_render_credits(app);
                    return true;
                }
            } else if(event->key == InputKeyLeft || event->key == InputKeyRight) {
                if(app->settings_menu_index == 1) {
                    if(event->key == InputKeyLeft) {
                        if(app->input_delay_ms == 200) {
                            app->input_delay_ms = 100;
                        } else if(app->input_delay_ms == 100) {
                            app->input_delay_ms = 50;
                        } else if(app->input_delay_ms == 50) {
                            app->input_delay_ms = 10;
                        } else {
                            app->input_delay_ms = 200;
                        }
                    } else {
                        if(app->input_delay_ms == 10) {
                            app->input_delay_ms = 50;
                        } else if(app->input_delay_ms == 50) {
                            app->input_delay_ms = 100;
                        } else if(app->input_delay_ms == 100) {
                            app->input_delay_ms = 200;
                        } else {
                            app->input_delay_ms = 10;
                        }
                    }
                    app_save_settings(app);
                    notification_message(app->notification, &sequence_success);
                } else if(app->settings_menu_index == 2) {
                    Storage* storage = furi_record_open(RECORD_STORAGE);
                    File* dir = storage_file_alloc(storage);
                    char layouts[MAX_LAYOUTS][64];
                    size_t layout_count = 0;
                    const char* current_layout = app->keyboard_layout;
                    int current_index = -1;
                    
                    if(storage_dir_open(dir, BADUSB_LAYOUTS_DIR)) {
                        FileInfo file_info;
        char name[64];
                        while(storage_dir_read(dir, &file_info, name, sizeof(name))) {
                            const char* ext = strrchr(name, '.');
                            if(ext && strcmp(ext, ".kl") == 0) {
                                if(layout_count < MAX_LAYOUTS) {
                                    strncpy(layouts[layout_count], name, sizeof(layouts[0]) - 1);
                                    layouts[layout_count][sizeof(layouts[0]) - 1] = '\0';
                                    if(strcmp(layouts[layout_count], current_layout) == 0) {
                                        current_index = (int)layout_count;
                                    }
                                    layout_count++;
                                }
                            }
                        }
                        storage_dir_close(dir);
                    }
                    storage_file_free(dir);
                    furi_record_close(RECORD_STORAGE);
                    
                    if(layout_count < MAX_LAYOUTS) {
                        for(size_t i = layout_count; i > 0; i--) {
                            strncpy(layouts[i], layouts[i-1], sizeof(layouts[0]) - 1);
                            layouts[i][sizeof(layouts[0]) - 1] = '\0';
                        }
                        strncpy(layouts[0], "en-US.kl", sizeof(layouts[0]) - 1);
                        layouts[0][sizeof(layouts[0]) - 1] = '\0';
                        layout_count++;
                        if(current_index >= 0) current_index++;
                        else if(strcmp(current_layout, "en-US.kl") == 0) current_index = 0;
                    }
                    
                    if(layout_count > 0) {
                        if(current_index < 0) current_index = 0;
                        
                        if(event->key == InputKeyLeft) {
                            current_index = (current_index > 0) ? current_index - 1 : (int)(layout_count - 1);
                        } else {
                            current_index = (int)((current_index + 1) % layout_count);
                        }
                        
                        strncpy(app->keyboard_layout, layouts[current_index], sizeof(app->keyboard_layout) - 1);
                        app->keyboard_layout[sizeof(app->keyboard_layout) - 1] = '\0';
                        app_save_settings(app);
                        notification_message(app->notification, &sequence_success);
                    }
                }
            }
            // Redraw settings view with scrolling
            app_render_settings(app);
            return true;
        }
        // Credits view: page navigation
        if(app->widget_state == 5) {
            if(event->key == InputKeyLeft) {
                if(app->credits_page > 0) {
                    app->credits_page--;
                } else {
                    app->credits_page = CREDITS_PAGES - 1; // Wrap to last page
                }
                app_render_credits(app);
                return true;
            } else if(event->key == InputKeyRight) {
                if(app->credits_page < CREDITS_PAGES - 1) {
                    app->credits_page++;
                } else {
                    app->credits_page = 0; // Wrap to first page
                }
                app_render_credits(app);
                return true;
            } else if(event->key == InputKeyBack) {
                // Return to settings menu
                app->widget_state = 4;
                app_render_settings(app);
                return true;
            }
            return true;
        }
        // Edit menu navigation (short presses)
        if(app->widget_state == 3) {
            if(event->key == InputKeyUp) {
                if(app->edit_menu_index > 0) app->edit_menu_index--;
            } else if(event->key == InputKeyDown) {
                if(app->edit_menu_index < 3) app->edit_menu_index++;
            } else if(event->key == InputKeyBack) {
                app->widget_state = 2; // back to list
            } else if(event->key == InputKeyOk) {
                if(app->edit_menu_index == 0) {
                    // Edit Name
                    app->edit_state = EditStateName;
                    text_input_reset(app->text_input);
                    text_input_set_header_text(app->text_input, "Edit Name");
                    text_input_set_result_callback(
                        app->text_input,
                        app_edit_text_result_callback,
                        app,
                        app->cards[app->edit_card_index].name,
                        sizeof(app->cards[app->edit_card_index].name),
                        false);
                    app_switch_to_view(app, ViewTextInput);
#ifdef HAS_MOMENTUM_SUPPORT
                    text_input_show_illegal_symbols(app->text_input, true);
#endif
                    return true;
                } else if(app->edit_menu_index == 1) {
                    // Edit Password
                    app->edit_state = EditStatePassword;
                    text_input_reset(app->text_input);
                    text_input_set_header_text(app->text_input, "Edit Password");
                    text_input_set_result_callback(
                        app->text_input,
                        app_edit_text_result_callback,
                        app,
                        app->cards[app->edit_card_index].password,
                        sizeof(app->cards[app->edit_card_index].password),
                        false);
#ifdef HAS_MOMENTUM_SUPPORT
                    text_input_show_illegal_symbols(app->text_input, true);
#endif
                    app_switch_to_view(app, ViewTextInput);
                    return true;
                } else if(app->edit_menu_index == 2) {
                    // Edit UID via hex keyboard (ByteInput)
                    app->edit_state = EditStateUid;
                    app->edit_uid_len = app->cards[app->edit_card_index].uid_len > 0 ?
                        app->cards[app->edit_card_index].uid_len : 4;
                    if(app->edit_uid_len > MAX_UID_LEN) app->edit_uid_len = MAX_UID_LEN;
                    memset(app->edit_uid_bytes, 0, sizeof(app->edit_uid_bytes));
                    memcpy(app->edit_uid_bytes, app->cards[app->edit_card_index].uid, app->edit_uid_len);
                    byte_input_set_header_text(app->byte_input, "Edit UID (hex)");
                    byte_input_set_result_callback(
                        app->byte_input,
                        app_edit_uid_byte_input_done,
                        NULL,
                        app,
                        app->edit_uid_bytes,
                        app->edit_uid_len);
                    app_switch_to_view(app, ViewByteInput);
                    return true;
                } else if(app->edit_menu_index == 3) {
                    // Delete selected card
                    if(app->edit_card_index < app->card_count) {
                        for(size_t i = app->edit_card_index; i + 1 < app->card_count; i++) {
                            app->cards[i] = app->cards[i + 1];
                        }
                        app->card_count--;
                        if(app->has_active_selection) {
                            if(app->active_card_index == app->edit_card_index) {
                                app->has_active_selection = false;
                            } else if(app->active_card_index > app->edit_card_index) {
                                app->active_card_index--;
                            }
                        }
                        if(app_save_cards(app)) {
                            notification_message(app->notification, &sequence_success);
                        } else {
                            notification_message(app->notification, &sequence_error);
                            FURI_LOG_E(TAG, "Failed to save after card deletion");
                        }
                    }
                    app->widget_state = 2; // back to list
                }
            }
            // Redraw edit menu or list depending on state
            if(app->widget_state == 3) {
                widget_reset(app->widget);
                widget_add_string_element(app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Edit Card");
                const char* items[] = {"Name", "Password", "UID", "Delete"};
                for(size_t i = 0; i < 4; i++) {
                    char line[32];
                    snprintf(line, sizeof(line), "%s %s", (i == app->edit_menu_index) ? ">" : " ", items[i]);
                    widget_add_string_element(app->widget, 0, 12 + i * 12, AlignLeft, AlignTop, FontSecondary, line);
                }
                return true;
            } else {
                // Back to list
                app_render_card_list(app);
                return true;
            }
        }
        // List navigation and deletion
        if(app->widget_state == 2) {
            if(event->key == InputKeyUp && app->card_count > 0) {
                if(app->selected_card > 0) {
                    app->selected_card--;
                    // Update scroll offset to keep selected card visible
                    if(app->selected_card < (size_t)app->card_list_scroll_offset) {
                        app->card_list_scroll_offset = (uint8_t)app->selected_card;
                    }
                } else {
                    // Wrap to bottom
                    app->selected_card = app->card_count - 1;
                    if(app->card_count > CARD_LIST_VISIBLE_ITEMS) {
                        app->card_list_scroll_offset = (uint8_t)(app->selected_card - (CARD_LIST_VISIBLE_ITEMS - 1));
                    } else {
                        app->card_list_scroll_offset = 0;
                    }
                }
            } else if(event->key == InputKeyDown && app->card_count > 0) {
                if(app->selected_card + 1 < app->card_count) {
                    app->selected_card++;
                    // Update scroll offset to keep selected card visible
                    if(app->selected_card >= (size_t)(app->card_list_scroll_offset + CARD_LIST_VISIBLE_ITEMS)) {
                        app->card_list_scroll_offset = (uint8_t)(app->selected_card - (CARD_LIST_VISIBLE_ITEMS - 1));
                    }
                } else {
                    // Wrap to top
                    app->selected_card = 0;
                    app->card_list_scroll_offset = 0;
                }
            } else if(event->key == InputKeyRight) {
                // Open file browser to import .nfc file
                furi_string_set(app->fb_output_path, "/ext/nfc");
                file_browser_start(app->file_browser, app->fb_output_path);
                app_switch_to_view(app, ViewFileBrowser);
                return true;
            } else if(event->key == InputKeyOk && app->card_count > 0) {
                // Short OK: set active selection to use on scan
                app->has_active_selection = true;
                app->active_card_index = app->selected_card;
                app_save_settings(app); // Save the selected card index
                notification_message(app->notification, &sequence_success);
            }
            // Refresh list
            app_render_card_list(app);
            return true;
        }
    } else if(event->type == InputTypeLong) {
        // Long-press OK: open edit menu for selected card
        if(app->widget_state == 2 && event->key == InputKeyOk && app->card_count > 0) {
            app->edit_card_index = app->selected_card;
            app->edit_menu_index = 0;
            app->widget_state = 3; // edit menu mode
            widget_reset(app->widget);
            widget_add_string_element(app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Edit Card");
            const char* items[] = {"Name", "Password", "UID", "Delete"};
            for(size_t i = 0; i < 4; i++) {
                char line[32];
                snprintf(line, sizeof(line), "%s %s", (i == app->edit_menu_index) ? ">" : " ", items[i]);
                widget_add_string_element(app->widget, 0, 12 + i * 12, AlignLeft, AlignTop, FontSecondary, line);
            }
            return true;
        }
        // Edit menu interactions
        if(app->widget_state == 3) {
            if(event->key == InputKeyUp) {
                if(app->edit_menu_index > 0) app->edit_menu_index--;
            } else if(event->key == InputKeyDown) {
                if(app->edit_menu_index < 3) app->edit_menu_index++;
            } else if(event->key == InputKeyBack) {
                // Return to list
                app->widget_state = 2;
            } else if(event->key == InputKeyOk) {
                if(app->edit_menu_index == 0) {
                    // Edit Name
                    app->edit_state = EditStateName;
                    text_input_reset(app->text_input);
                    text_input_set_header_text(app->text_input, "Edit Name");
                    text_input_set_result_callback(
                        app->text_input,
                        app_edit_text_result_callback,
                        app,
                        app->cards[app->edit_card_index].name,
                        sizeof(app->cards[app->edit_card_index].name),
                        false);
                    app_switch_to_view(app, ViewTextInput);
#ifdef HAS_MOMENTUM_SUPPORT
                    text_input_show_illegal_symbols(app->text_input, true);
#endif
                    return true;
                } else if(app->edit_menu_index == 1) {
                    // Edit Password
                    app->edit_state = EditStatePassword;
                    text_input_reset(app->text_input);
                    text_input_set_header_text(app->text_input, "Edit Password");
                    text_input_set_result_callback(
                        app->text_input,
                        app_edit_text_result_callback,
                        app,
                        app->cards[app->edit_card_index].password,
                        sizeof(app->cards[app->edit_card_index].password),
                        false);
#ifdef HAS_MOMENTUM_SUPPORT
                    text_input_show_illegal_symbols(app->text_input, true);
#endif
                    app_switch_to_view(app, ViewTextInput);
                    return true;
                } else if(app->edit_menu_index == 2) {
                    // Edit UID via hex keyboard (ByteInput)
                    app->edit_state = EditStateUid;
                    // Prefill bytes and len
                    app->edit_uid_len = app->cards[app->edit_card_index].uid_len > 0 ?
                        app->cards[app->edit_card_index].uid_len : 4;
                    if(app->edit_uid_len > MAX_UID_LEN) app->edit_uid_len = MAX_UID_LEN;
                    memset(app->edit_uid_bytes, 0, sizeof(app->edit_uid_bytes));
                    memcpy(app->edit_uid_bytes, app->cards[app->edit_card_index].uid, app->edit_uid_len);
                    byte_input_set_header_text(app->byte_input, "Edit UID (hex)");
                    byte_input_set_result_callback(
                        app->byte_input,
                        app_edit_uid_byte_input_done,
                        NULL,
                        app,
                        app->edit_uid_bytes,
                        app->edit_uid_len);
                    app_switch_to_view(app, ViewByteInput);
                    return true;
                } else if(app->edit_menu_index == 3) {
                    // Delete
                    if(app->edit_card_index < app->card_count) {
                        for(size_t i = app->edit_card_index; i + 1 < app->card_count; i++) {
                            app->cards[i] = app->cards[i + 1];
                        }
                        app->card_count--;
                        if(app->has_active_selection) {
                            if(app->active_card_index == app->edit_card_index) {
                                app->has_active_selection = false;
                            } else if(app->active_card_index > app->edit_card_index) {
                                app->active_card_index--;
                            }
                        }
                        if(app_save_cards(app)) {
                            notification_message(app->notification, &sequence_success);
                        } else {
                            notification_message(app->notification, &sequence_error);
                            FURI_LOG_E(TAG, "Failed to save after card deletion");
                        }
                    }
                    // Return to list
                    app->widget_state = 2;
                }
            }
            // Redraw edit menu
            if(app->widget_state == 3) {
                widget_reset(app->widget);
                widget_add_string_element(app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Edit Card");
                const char* items[] = {"Name", "Password", "UID", "Delete"};
                for(size_t i = 0; i < 4; i++) {
                    char line[32];
                    snprintf(line, sizeof(line), "%s %s", (i == app->edit_menu_index) ? ">" : " ", items[i]);
                    widget_add_string_element(app->widget, 0, 12 + i * 12, AlignLeft, AlignTop, FontSecondary, line);
                }
                return true;
            }
            // If we fell through and switched back to list
            // Render list view
            widget_reset(app->widget);
            widget_add_string_element(app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Cards");
            if(app->card_count == 0) {
                widget_add_string_element(app->widget, 0, 20, AlignLeft, AlignTop, FontSecondary, "No cards stored");
            } else {
                for(size_t i = 0; i < app->card_count && i < 10; i++) {
                    char line[64];
                    // Show ">" for currently navigated card, "*" for active/selected card
                    const char* nav_marker = (i == app->selected_card) ? ">" : " ";
                    const char* active_marker = (app->has_active_selection && i == app->active_card_index) ? "*" : " ";
                    snprintf(line, sizeof(line), "%s%s %zu. %s", 
                            nav_marker, active_marker, i + 1, app->cards[i].name);
                    widget_add_string_element(app->widget, 0, 10 + i * 10, AlignLeft, AlignTop, FontSecondary, line);
                }
            }
            widget_add_string_element(app->widget, 0, 54, AlignLeft, AlignTop, FontSecondary, "OK=Sel  Hold OK=Edit");
        return true;
        }
    }
    return false;
}

// Main app
int32_t nfc_login(void* p) {
    UNUSED(p);
    
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    
    // Initialize GUI
    app->gui = furi_record_open(RECORD_GUI);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    
    // View dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, app_navigation_callback);
    
    // Submenu
    app->submenu = submenu_alloc();
    submenu_add_item(app->submenu, "Add NFC Card", SubmenuAddCard, submenu_callback, app);
    submenu_add_item(app->submenu, "List Cards", SubmenuListCards, submenu_callback, app);
    submenu_add_item(app->submenu, "Start Scan", SubmenuStartScan, submenu_callback, app);
    submenu_add_item(app->submenu, "Settings", SubmenuSettings, submenu_callback, app);
    view_dispatcher_add_view(app->view_dispatcher, ViewSubmenu, submenu_get_view(app->submenu));
    
    // Text input
    app->text_input = text_input_alloc();
#ifdef HAS_MOMENTUM_SUPPORT
    if(app->text_input) text_input_show_illegal_symbols(app->text_input, true);
#endif
    view_dispatcher_add_view(app->view_dispatcher, ViewTextInput, text_input_get_view(app->text_input));
    // TextInput handles its own input; do not override its view context
    
    // Widget
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ViewWidget, widget_get_view(app->widget));
    
    // Widget input handler to stop scanning
    view_set_input_callback(widget_get_view(app->widget), app_widget_view_input);
    view_set_context(widget_get_view(app->widget), app);
    
    // File browser
    app->fb_output_path = furi_string_alloc();
    app->file_browser = file_browser_alloc(app->fb_output_path);
    file_browser_configure(
        app->file_browser,
        ".nfc",
        "/ext/nfc",
        true,  // skip assets
        true,  // hide dot files
        NULL,
        false  // show extension
    );
    file_browser_set_callback(app->file_browser, app_file_browser_callback, app);
    view_dispatcher_add_view(app->view_dispatcher, ViewFileBrowser, file_browser_get_view(app->file_browser));
    
    // Byte input
    app->byte_input = byte_input_alloc();
    view_dispatcher_add_view(app->view_dispatcher, ViewByteInput, byte_input_get_view(app->byte_input));
    
    // Initialize layout settings
    // Load settings (includes keyboard layout)
    app_load_settings(app);
    
    // Ensure crypto key exists early (will generate if needed)
    // This prevents blocking during save operations
    ensure_crypto_key();
    
    // Load cards
    app_load_cards(app);
    
    // Start with submenu
    app_switch_to_view(app, ViewSubmenu);
    
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
    
    if(app->previous_usb_config) {
        deinitialize_hid_with_restore(app->previous_usb_config);
    }
    
    view_dispatcher_remove_view(app->view_dispatcher, ViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, ViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, ViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, ViewFileBrowser);
    view_dispatcher_remove_view(app->view_dispatcher, ViewByteInput);
    
    submenu_free(app->submenu);
    text_input_free(app->text_input);
    widget_free(app->widget);
    file_browser_free(app->file_browser);
    furi_string_free(app->fb_output_path);
    byte_input_free(app->byte_input);
    view_dispatcher_free(app->view_dispatcher);
    
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    
    free(app);
    
    return 0;
}

