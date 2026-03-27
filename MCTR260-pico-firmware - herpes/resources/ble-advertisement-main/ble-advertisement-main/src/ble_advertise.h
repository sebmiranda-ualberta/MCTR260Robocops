#pragma once

void ble_start_advertising(const char *name);
void ble_task(void);   // call this repeatedly if using pico_cyw43_arch_none
