cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(VCPcontroller)

target_sources(app PRIVATE
    src/main.c
    src/ble_manager.c
    src/vcp_controller.c
    src/battery_reader.c
)
