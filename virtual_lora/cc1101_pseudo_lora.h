#pragma once

#include <furi_hal.h>
#include <lib/drivers/cc1101.h>

// CC1101 register configuration for "Pseudo-LoRa" spectral sensing
typedef struct {
    uint8_t reg;
    uint8_t value;
    const char* description;
} CC1101PseudoLoraConfig;

// Optimized register settings for wideband energy detection at 868 MHz
static const CC1101PseudoLoraConfig cc1101_pseudo_lora_config[] = {
    {CC1101_IOCFG2, 0x0D, "GDO2: Serial Data Output"},
    {CC1101_IOCFG0, 0x06, "GDO0: Sync Word / Packet End"},
    {CC1101_FIFOTHR, 0x47, "RX FIFO Threshold: 32 bytes"},
    {CC1101_PKTCTRL0, 0x32, "Async Serial Mode, No Whiten"},
    {CC1101_FSCTRL1, 0x06, "IF Frequency: ~152 kHz"},
    {CC1101_FREQ2, 0x21, "Frequency Control (868 MHz)"},
    {CC1101_FREQ1, 0x62, "Frequency Control"},
    {CC1101_FREQ0, 0x76, "Frequency Control"},
    {CC1101_MDMCFG4, 0x2D, "RX Bandwidth: 270 kHz"},
    {CC1101_MDMCFG3, 0x93, "Data Rate: ~4.8 kBaud"},
    {CC1101_MDMCFG2, 0x30, "ASK/OOK Modulation"},
    {CC1101_MDMCFG1, 0x22, "Channel spacing"},
    {CC1101_MDMCFG0, 0xF8, "Channel spacing"},
    {CC1101_MCSM0, 0x18, "Auto-calibrate IDLE->RX/TX"},
    {CC1101_FOCCFG, 0x16, "Frequency Offset Compensation"},
    {CC1101_AGCCTRL2, 0x07, "AGC Control: Max Gain"},
    {CC1101_AGCCTRL1, 0x00, "AGC Control"},
    {CC1101_AGCCTRL0, 0x91, "AGC Control"},
    {0xFF, 0xFF, NULL} // End marker
};

bool cc1101_configure_pseudo_lora_mode(const FuriHalSpiBusHandle* handle);
bool cc1101_start_spectral_capture(const FuriHalSpiBusHandle* handle);
void cc1101_stop_spectral_capture(const FuriHalSpiBusHandle* handle);