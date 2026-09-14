#pragma once

#include <furi.h>
#include <toolbox/pipe.h>

void clicontrol_hijack(size_t tx_size, size_t rx_size);
void clicontrol_unhijack(bool persist);

extern PipeSide* cli_tx_stream;
extern PipeSide* cli_rx_stream;
