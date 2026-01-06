#include "cc1101_pseudo_lora.h"
#include <furi_hal_subghz.h>

#define TAG "CC1101PseudoLora"

bool cc1101_configure_pseudo_lora_mode(const FuriHalSpiBusHandle* handle) {
    FURI_LOG_I(TAG, "Configuring CC1101 for pseudo-LoRa spectral sensing");
    
    // Reset CC1101
    cc1101_reset(handle);
    furi_delay_ms(10);
    
    // Apply pseudo-LoRa configuration
    const CC1101PseudoLoraConfig* config = cc1101_pseudo_lora_config;
    while(config->reg != 0xFF) {
        cc1101_write_reg(handle, config->reg, config->value);
        FURI_LOG_D(TAG, "Set %02X = %02X (%s)", config->reg, config->value, config->description);
        config++;
    }
    
    // Verify configuration
    uint8_t test_reg;
    cc1101_read_reg(handle, CC1101_MDMCFG2, &test_reg);
    if(test_reg != 0x30) {
        FURI_LOG_E(TAG, "Configuration verification failed");
        return false;
    }
    
    // Calibrate
    cc1101_calibrate(handle);
    if(!cc1101_wait_status_state(handle, CC1101StateIDLE, 1000)) {
        FURI_LOG_E(TAG, "Calibration timeout");
        return false;
    }
    
    FURI_LOG_I(TAG, "CC1101 configured for spectral sensing");
    return true;
}

bool cc1101_start_spectral_capture(const FuriHalSpiBusHandle* handle) {
    FURI_LOG_I(TAG, "Starting spectral capture mode");
    
    // Flush RX FIFO
    cc1101_flush_rx(handle);
    
    // Switch to RX mode for continuous reception
    cc1101_switch_to_rx(handle);
    
    if(!cc1101_wait_status_state(handle, CC1101StateRX, 1000)) {
        FURI_LOG_E(TAG, "Failed to enter RX mode");
        return false;
    }
    
    FURI_LOG_I(TAG, "Spectral capture started");
    return true;
}

void cc1101_stop_spectral_capture(const FuriHalSpiBusHandle* handle) {
    FURI_LOG_I(TAG, "Stopping spectral capture");
    
    // Switch to idle
    cc1101_switch_to_idle(handle);
    cc1101_wait_status_state(handle, CC1101StateIDLE, 1000);
    
    // Flush buffers
    cc1101_flush_rx(handle);
    cc1101_flush_tx(handle);
}