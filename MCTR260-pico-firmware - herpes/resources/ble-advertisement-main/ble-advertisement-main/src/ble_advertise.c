#include <stdio.h>
#include <string.h>
#include "btstack.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "inventor.h"     // generated from inventor.gatt
extern const uint8_t profile_data[];


#define APP_AD_FLAGS 0x06  // LE General Discoverable + BR/EDR Not Supported

static btstack_packet_callback_registration_t hci_event_callback_registration;

// Advertising payload (max 31 bytes total)
static uint8_t adv_data[31];
static uint8_t adv_len = 0;

static absolute_time_t last_blink;
static bool led_on = false;

static hci_con_handle_t connection_handle = HCI_CON_HANDLE_INVALID;
static bool connected = false;


void heartbeat_blink(void) { // to indicate we're alive
    int interval_us = connected ? 120000 : 500000;   // fast blink when connected
    if (absolute_time_diff_us(last_blink, get_absolute_time()) > 500000) {
        led_on = !led_on;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
        last_blink = get_absolute_time();
    }
}

static void build_adv_data_with_name(const char *name) {
    memset(adv_data, 0, sizeof(adv_data));
    adv_len = 0;

    // Flags
    adv_data[adv_len++] = 2;                       // length (type + 1 byte)
    adv_data[adv_len++] = BLUETOOTH_DATA_TYPE_FLAGS;
    adv_data[adv_len++] = APP_AD_FLAGS;

    // Complete Local Name
    size_t name_len = strlen(name);
    if (name_len > 29 - adv_len) {                 // keep within 31 total
        name_len = 29 - adv_len;
    }

    adv_data[adv_len++] = (uint8_t)(name_len + 1); // length (type + name bytes)
    adv_data[adv_len++] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&adv_data[adv_len], name, name_len);
    adv_len += (uint8_t)name_len;
}

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;

    if (packet_type != HCI_EVENT_PACKET) return;

    uint16_t event_type = hci_event_packet_get_type(packet);
    switch(event_type){
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) break;

            printf("BTstack working. Starting advertising as: %s\n", DEVICE_NAME);

            // Advertising params (interval in 0.625ms units; 800 -> 500ms)
            uint16_t adv_int_min = 800;
            uint16_t adv_int_max = 800;
            uint8_t adv_type = 0; // connectable undirected
            bd_addr_t null_addr;
            memset(null_addr, 0, 6);

            gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type,
                                            0, null_addr, 0x07, 0x00);
            gap_advertisements_set_data(adv_len, adv_data);
            
            // sanity check 3
            gap_scan_response_set_data(adv_len, adv_data);

            gap_advertisements_enable(1);

            // sanity check 2
            bd_addr_t local;
            gap_local_bd_addr(local);
            printf("Local BD_ADDR: %s\n", bd_addr_to_str(local));

            break;
        
        case HCI_EVENT_LE_META: {
            uint8_t subevent = hci_event_le_meta_get_subevent_code(packet);
            if (subevent == HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
                bd_addr_t addr;
                gap_event_advertising_report_get_address(packet, addr); // if this doesn't compile, use the alternative below

                connection_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                connected = true;

                bd_addr_t peer;
                hci_subevent_le_connection_complete_get_peer_address(packet, peer);

                printf("BLE CONNECTED: handle=0x%04x, peer=%s\n",
                    connection_handle, bd_addr_to_str(peer));
            }
            break;
        }

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            connected = false;
            connection_handle = HCI_CON_HANDLE_INVALID;
            printf("BLE DISCONNECTED\n");
            break;

        default:
            break;
    }
}

static uint16_t att_read_callback(hci_con_handle_t connection_handle,
                                  uint16_t att_handle, uint16_t offset,
                                  uint8_t *buffer, uint16_t buffer_size) {
    (void)connection_handle;

    if (att_handle == ATT_CHARACTERISTIC_GAP_DEVICE_NAME_01_VALUE_HANDLE) {
        const char *name = DEVICE_NAME;   // or the `name` you store globally
        return att_read_callback_handle_blob((const uint8_t*)name, strlen(name),
                                             offset, buffer, buffer_size);
    }
    return 0;
}


void ble_start_advertising(const char *name) {
    // Build adv payload first (so packet handler can just enable it)
    build_adv_data_with_name(name);

    // sanity check 1
    printf("ADV will use name: %s (adv_len=%u)\n", name, adv_len);
    printf("ADV bytes: ");
    for (int i = 0; i < adv_len; i++) printf("%02X ", adv_data[i]);
    printf("\n");

    l2cap_init();
    sm_init();
    att_server_init(profile_data, NULL, NULL);


    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    hci_power_control(HCI_POWER_ON);

    // Keep the background/BTstack running
    while (true) {
        heartbeat_blink(); // LED proof of life
        async_context_poll(cyw43_arch_async_context());
        async_context_wait_for_work_until(cyw43_arch_async_context(), make_timeout_time_ms(50));
    }
}
