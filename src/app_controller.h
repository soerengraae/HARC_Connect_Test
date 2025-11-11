#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include "ble_manager.h"
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/settings/settings.h>

/**
 * @brief Connection strategies based on bonded device count
 * @param STRATEGY_FIRST_TIME_PAIRING State at first time use - no bonded devices
 * @param STRATEGY_ONE_BONDED_DEVICE One bonded device exists - connect to it
 * @param STRATEGY_TWO_BONDED_DEVICES Two bonded devices exist - connect to both
 * @param STRATEGY_ILLEGAL_STATE More than two bonded devices - error state
 */
enum connection_strategy {
    STRATEGY_FIRST_TIME_PAIRING,
    STRATEGY_ONE_BONDED_DEVICE,
    STRATEGY_TWO_BONDED_DEVICES,
    STRATEGY_ILLEGAL_STATE
};

/* Connection state machine states for dual-device coordination */
enum sm_state {
    SM_IDLE, /* No active connection process */
    SM_WAKE, /* Wake up and determine state */
    SM_FIRST_TIME_USE, /* First device bonding/discovering */
    SM_WAIT_FOR_CSIP_DISCOVERY, /* Waiting for CSIP discovery to complete */
};

extern uint8_t strategy;

int8_t app_controller_notify_system_ready();
int8_t app_controller_notify_ble_cmd_complete(uint8_t device_id, enum ble_cmd_type type, int8_t err_code);
int8_t app_controller_notify_device_connected(uint8_t device_id);
int8_t app_controller_notify_device_ready(uint8_t device_id);
int8_t app_controller_notify_scan_complete();

#endif /* CONNECTION_MANAGER_H */