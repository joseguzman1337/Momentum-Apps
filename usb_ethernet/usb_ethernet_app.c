#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>

#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>
#include <lwip/dhcp.h>
#include <lwip/api.h> // For netconn/sockets if needed
#include <lwip/apps/http_client.h>

// We need the prototype for our netif init
// Defined in lib/lwip/port/furi_lwip_netif.h
// But we cannot easily include it if header path not set.
// Extern it here for now.
extern err_t ethernetif_init(struct netif *netif);

// Defined in targets/f7/furi_hal/furi_hal_usb_cdc.c
extern FuriHalUsbInterface usb_cdc_ecm;

typedef void (*FuriHalUsbNetworkReceiveCallback)(uint8_t* buffer, uint16_t len, void* context);
void furi_hal_usb_ethernet_set_received_callback(FuriHalUsbNetworkReceiveCallback callback, void* context);
void furi_hal_usb_ethernet_send(uint8_t* buffer, uint16_t len);

struct netif furi_netif;
static char http_status[128] = "Press OK to Test HTTP";

static void test_http_result_cb(void *arg, httpc_result_t httpc_result, u32_t rx_content_len, u32_t srv_res, err_t err) {
    UNUSED(arg);
    snprintf(http_status, sizeof(http_status), "Res: %d L:%lu C:%lu E:%d", httpc_result, rx_content_len, srv_res, err);
    FURI_LOG_I("HTTP", "%s", http_status);
}

void usb_ethernet_draw_callback(Canvas* canvas, void* ctx) {
    UNUSED(ctx);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 10, 10, "USB Ethernet Active");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 10, 24, "LwIP Stack: Running");
    
    char buf[64];
    if (netif_is_link_up(&furi_netif)) {
        canvas_draw_str(canvas, 10, 36, "Link: UP");
        snprintf(buf, sizeof(buf), "IP: %s", ip4addr_ntoa(netif_ip4_addr(&furi_netif)));
        canvas_draw_str(canvas, 10, 48, buf);
    } else {
        canvas_draw_str(canvas, 10, 36, "Link: DOWN");
        canvas_draw_str(canvas, 10, 48, "Waiting for host...");
    }
    
    canvas_draw_str(canvas, 10, 60, "Press Back to Exit");
    canvas_draw_str(canvas, 10, 54, http_status);
}

void usb_ethernet_input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

static void netif_status_callback(struct netif *netif) {
    FURI_LOG_I("LwIP", "Netif status changed: %s", ip4addr_ntoa(netif_ip4_addr(netif)));
}

int32_t usb_ethernet_app(void* p) {
    UNUSED(p);
    
    // Enable Ethernet Mode
    if(furi_hal_usb_is_locked()) {
        furi_hal_usb_unlock();
    }
    furi_hal_usb_disable();
    furi_delay_ms(500); 
    
    if(!furi_hal_usb_set_config(&usb_cdc_ecm, NULL)) {
        FURI_LOG_E("UsbEthernet", "Failed to set USB config");
        return -1;
    }
    
    // Init LwIP
    FURI_LOG_I("LwIP", "Initializing TCP/IP");
    tcpip_init(NULL, NULL);
    
    // Add Netif
    ip4_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 0, 0, 0, 0);
    IP4_ADDR(&netmask, 0, 0, 0, 0);
    IP4_ADDR(&gw, 0, 0, 0, 0);
    
    netif_add(&furi_netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, tcpip_input);
    netif_set_default(&furi_netif);
    netif_set_status_callback(&furi_netif, netif_status_callback);
    
    // Bring up
    netif_set_up(&furi_netif);
    netif_set_link_up(&furi_netif); // We assume USB link is up if enumerated
    
    // Start DHCP
    dhcp_start(&furi_netif);
    
    // GUI
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, usb_ethernet_draw_callback, NULL);
    view_port_input_callback_set(view_port, usb_ethernet_input_callback, event_queue);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    
    // Update loop
    InputEvent event;
    while(furi_message_queue_get(event_queue, &event, 500) != FuriStatusError) {
        // Redraw every 500ms to update IP status
        view_port_update(view_port);
        
        if(event.key == InputKeyBack && event.type == InputTypeShort) {
            break;
        }
        if(event.key == InputKeyOk && event.type == InputTypeShort) {
             snprintf(http_status, sizeof(http_status), "Connecting to google.com...");
             FURI_LOG_I("HTTP", "Starting Request");
             
             httpc_connection_t settings;
             memset(&settings, 0, sizeof(settings));
             settings.use_proxy = 0;
             settings.result_fn = test_http_result_cb;
             
             err_t err = httpc_get_file_dns("google.com", 80, "/", &settings, NULL, NULL, NULL);
             if(err != ERR_OK) {
                 snprintf(http_status, sizeof(http_status), "Start Err: %d", err);
             }
        }
    }
    
    // Cleanup
    furi_hal_usb_ethernet_set_received_callback(NULL, NULL); // Stop callbacks into Unloaded RAM!
    
    // Shutdown LwIP
    dhcp_stop(&furi_netif);
    netif_remove(&furi_netif);
    
    // Cleanup USB
    furi_hal_usb_disable();
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_record_close(RECORD_GUI);
    
    return 0;
}
