#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "ble_advertise.h"

// // Pick a name you can easily spot in your phone scanner:
// #define DEVICE_NAME "MCTR-Inventor"

int main() {
    stdio_init_all();
    sleep_ms(2000);

    // Init CYW43 (needed for BLE)
    if (cyw43_arch_init()) {
        printf("cyw43_arch_init failed\n");
        while (1) tight_loop_contents();
    }

    // Wait until the PC actually connects to USB-serial (so printf shows up)
    absolute_time_t deadline = make_timeout_time_ms(3000);
    while (!stdio_usb_connected()){//&& absolute_time_diff_us(get_absolute_time(), deadline) < 0) {
        sleep_ms(100);
    }
    
    printf("Booted. Starting BLE advertising...\n");
    // Start BLE advertising (non-returning - BTstack loop)
    ble_start_advertising(DEVICE_NAME);

    // Should never get here
    while (true) {
        heartbeat_blink();   // LED proof of life
        async_context_poll(cyw43_arch_async_context());
        async_context_wait_for_work_until(
            cyw43_arch_async_context(),
            make_timeout_time_ms(10)
        );
        // tight_loop_contents();
    }
}
