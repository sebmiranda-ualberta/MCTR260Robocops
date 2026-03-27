cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)
# include(btstack_config.h)

project(mctr_ble_advertise C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Pico W board
set(PICO_BOARD pico_w)

# Initialize Pico SDK
pico_sdk_init()

add_executable(inventor_ble
    src/main.c
    src/ble_advertise.c
)

set(DEVICE_NAME "MCTR-Inventor" CACHE STRING "BLE device name")

configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/src/inventor.gatt.in
  ${CMAKE_CURRENT_BINARY_DIR}/inventor.gatt
  @ONLY
)

pico_btstack_make_gatt_header(inventor_ble PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/inventor.gatt
)

target_compile_definitions(inventor_ble PRIVATE DEVICE_NAME=\"${DEVICE_NAME}\")
# Alternatively, if you have a static .gatt file, you can use:
# pico_btstack_make_gatt_header(inventor_ble PRIVATE
#   ${CMAKE_CURRENT_LIST_DIR}/src/inventor.gatt
# )

target_include_directories(inventor_ble PRIVATE
    ${PICO_SDK_PATH}/src/rp2_common/pico_cyw43_arch/include
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}    # so btstack_config_common.h in project root is found
)

# BLE + CYW43 + stdlib
target_link_libraries(inventor_ble
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_ble
    pico_btstack_cyw43
)

# Enable USB serial
pico_enable_stdio_usb(inventor_ble 1)
pico_enable_stdio_uart(inventor_ble 0)

pico_add_extra_outputs(inventor_ble)
