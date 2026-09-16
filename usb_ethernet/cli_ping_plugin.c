#include <furi.h>
#include <toolbox/cli/cli_command.h>

#include "usb_ethernet_service.h"

static void cli_ping_execute(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    UNUSED(context);

    if(furi_string_size(args) == 0) {
        cli_print_usage("ping", "<host>", "");
        return;
    }

    const char* host = furi_string_get_cstr(args);
    printf("Pinging %s...\r\n", host);

    bool success = false;
    if(furi_record_exists(RECORD_USB_ETHERNET)) {
        UsbEthernetService* service = furi_record_open(RECORD_USB_ETHERNET);
        success = service->ping(service->context, host, 4, 2000);
        furi_record_close(RECORD_USB_ETHERNET);
    }

    printf(success ? "Ping success!\r\n" : "Ping failed.\r\n");
}

CLI_COMMAND_INTERFACE(ping, cli_ping_execute, CliCommandFlagParallelSafe, 768, "cli");
