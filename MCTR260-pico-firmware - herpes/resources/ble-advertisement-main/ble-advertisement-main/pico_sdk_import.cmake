# Minimal Pico SDK import helper
# Uses PICO_SDK_PATH environment variable

if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
  set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()

if (NOT PICO_SDK_PATH)
  message(FATAL_ERROR "PICO_SDK_PATH not set. Export it, e.g. export PICO_SDK_PATH=~/pico-sdk")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
