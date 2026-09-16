#include <furi.h>
#include <furi_hal_usb.h>
#include <toolbox/cli/cli_command.h>

#include "furi_hal_usb_eth.h"

static void cli_ping_execute(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    UNUSED(context);

    if(furi_string_size(args) == 0) {
        cli_print_usage("ping", "<host>", "");
        return;
    }

    const char* host = furi_string_get_cstr(args);
    printf("Pinging %s...\r\n", host);

    FuriHalUsbInterface* previous_usb = furi_hal_usb_get_config();
    if(furi_hal_usb_is_locked()) furi_hal_usb_unlock();

    bool success = false;
    if(furi_hal_usb_set_config(&usb_eth, NULL)) {
        success = furi_hal_usb_eth_ping(host, 4, 2000);
        furi_hal_usb_set_config(previous_usb, NULL);
    }

    printf(success ? "Ping success!\r\n" : "Ping failed.\r\n");
}

CLI_COMMAND_INTERFACE(ping, cli_ping_execute, CliCommandFlagParallelSafe, 768, "cli");
