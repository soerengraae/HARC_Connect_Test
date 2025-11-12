cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(VCPcontroller)

target_sources(app PRIVATE
    src/main.c
    src/ble_manager.c
    src/vcp_controller.c
    src/battery_reader.c
    src/csip_coordinator.c
    src/app_controller.c
    src/devices_manager.c
    src/oled_display.c
    src/button_handler.c
    src/display_manager.c
    src/has_controller.c
)
