#ifndef BATTERY_READER_H
#define BATTERY_READER_H

#include "ble_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

/* Battery Service UUID */
// #define BT_UUID_BAS_VAL 0x180F
#define BT_UUID_BAS BT_UUID_DECLARE_16(BT_UUID_BAS_VAL)

/* Battery Level Characteristic UUID */
// #define BT_UUID_BAS_BATTERY_LEVEL_VAL 0x2A19
#define BT_UUID_BAS_BATTERY_LEVEL BT_UUID_DECLARE_16(BT_UUID_BAS_BATTERY_LEVEL_VAL)

/**
 * @brief Discover Battery Service on a connected device
 * 
 * @return 0 on success, negative error code on failure
 */
int battery_discover();

/**
 * @brief Read battery level from discovered battery service
 * 
 * @param conn Pointer to the BLE connection
 * @return 0 on success, negative error code on failure
 */
int battery_read_level(uint8_t device_id);

/**
 * @brief Subscribe to battery level notifications
 * 
 * @param conn Pointer to the BLE connection
 * @return 0 on success, negative error code on failure
 */
int battery_subscribe_notifications();

/**
 * @brief Reset battery reader state
 */
void battery_reader_reset(uint8_t device_id);

int battery_reader_init();

#endif /* BATTERY_READER_H */