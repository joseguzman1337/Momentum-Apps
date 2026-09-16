from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_usb_callback_is_removed_before_rx_resources_are_freed():
    source = (ROOT / "usb_ethernet_private.c").read_text()
    deinit_start = source.index("static void eth_deinit(usbd_device* dev) {")
    deinit = source[deinit_start : source.index("static void eth_on_wakeup", deinit_start)]
    assert deinit.index("usbd_reg_endpoint(dev, ETH_RNDIS_RX_EP, NULL)") < deinit.index(
        "furi_stream_buffer_free"
    )
    assert deinit.index("tcpip_shutdown()") < deinit.index("usbd_reg_config(dev, NULL)")


def test_tcpip_shutdown_releases_task_and_rtos_objects():
    source = (ROOT / "lib/lwip/api_tcpip.c").read_text()
    shutdown = source[source.index("tcpip_shutdown(void)") : source.index("pbuf_free_int")]
    for operation in (
        "sys_mbox_post(&tcpip_mbox, NULL)",
        "sys_arch_sem_wait(&tcpip_shutdown_done, 0)",
        "vTaskDelete",
        "sys_sem_free",
        "sys_mbox_free",
        "tcpip_thread_running = 0",
    ):
        assert operation in shutdown


def test_cli_ping_is_a_record_client_not_a_second_stack_owner():
    manifest = (ROOT / "application.fam").read_text()
    cli_app = manifest[manifest.index('appid="cli_ping"') :]
    assert 'sources=["cli_ping_plugin.c"]' in cli_app
    assert "fap_private_libs" not in cli_app

    source = (ROOT / "cli_ping_plugin.c").read_text()
    assert "RECORD_USB_ETHERNET" in source
    assert "furi_hal_usb_set_config" not in source
    assert "furi_hal_usb_eth_ping" not in source
