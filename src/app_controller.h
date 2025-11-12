#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include "ble_manager.h"
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/settings/settings.h>

/* Connection state machine states for dual-device coordination */
enum sm_state {
    SM_IDLE, /* No active connection process */
    SM_WAKE, /* Wake up and determine state */
    SM_FIRST_TIME_USE, /* First device bonding/discovering */
    SM_SINGLE_BONDED_DEVICE, /* Managing single bonded device */
    SM_DUAL_DEVICE_OPERATION, /* Managing two bonded devices */
    SM_
};

extern uint8_t strategy;

int8_t app_controller_notify_system_ready();
int8_t app_controller_notify_ble_cmd_complete(uint8_t device_id, enum ble_cmd_type type, int8_t err_code);
int8_t app_controller_notify_device_connected(uint8_t device_id);
int8_t app_controller_notify_device_ready(uint8_t device_id);
int8_t app_controller_notify_scan_complete();
int8_t app_controller_notify_csip_discovered(uint8_t device_id, int8_t err);
int8_t app_controller_notify_csip_member_match(uint8_t device_id, int8_t err);
int8_t app_controller_notify_volume_up_button_pressed();
int8_t app_controller_notify_volume_down_button_pressed();
int8_t app_controller_notify_pair_button_pressed();
int8_t app_controller_notify_preset_button_pressed();

#endif /* CONNECTION_MANAGER_H */