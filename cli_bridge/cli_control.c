#include "cli_control.h"

#include <cli/cli.h>
#include <toolbox/cli/shell/cli_shell.h>

PipeSide* cli_tx_stream = NULL;
PipeSide* cli_rx_stream = NULL;

static PipeSide* cli_shell_pipe = NULL;
static CliShell* cli_shell = NULL;

static void cli_gui_motd(void* context) {
    PipeSide* pipe = context;
    static const char banner[] = "Momentum CLI GUI\r\n";
    pipe_send(pipe, banner, sizeof(banner) - 1);
}

void clicontrol_hijack(size_t tx_size, size_t rx_size) {
    if(cli_shell) return;

    const size_t buffer_size = MAX(tx_size, rx_size);
    PipeSideBundle bundle = pipe_alloc(buffer_size, 1);
    cli_tx_stream = bundle.alices_side;
    cli_rx_stream = bundle.alices_side;
    cli_shell_pipe = bundle.bobs_side;

    CliRegistry* registry = furi_record_open(RECORD_CLI);
    cli_shell = cli_shell_alloc(cli_gui_motd, cli_shell_pipe, cli_shell_pipe, registry, NULL);
    furi_record_close(RECORD_CLI);
    cli_shell_start(cli_shell);
}

void clicontrol_unhijack(bool persist) {
    UNUSED(persist);
    if(!cli_shell) return;

    static const char exit_command[] = "\x03\x03\r\nexit\r\n";
    pipe_send(cli_rx_stream, exit_command, sizeof(exit_command) - 1);
    cli_shell_join(cli_shell);
    cli_shell_free(cli_shell);
    cli_shell = NULL;

    pipe_free(cli_shell_pipe);
    pipe_free(cli_tx_stream);
    cli_shell_pipe = NULL;
    cli_tx_stream = NULL;
    cli_rx_stream = NULL;
}
