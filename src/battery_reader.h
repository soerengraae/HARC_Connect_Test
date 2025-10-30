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
int battery_read_level(struct connection_context *ctx);

/**
 * @brief Subscribe to battery level notifications
 * 
 * @param conn Pointer to the BLE connection
 * @return 0 on success, negative error code on failure
 */
int battery_subscribe_notifications(struct bt_conn *conn);

/**
 * @brief Reset battery reader state
 */
void battery_reader_reset(void);

/**
 * @brief Get the read battery level
 * 
 * @return Battery level percentage (0-100), or -1 if not available
 */
int battery_get_level(void);

/* Global state */
extern bool battery_discovered;
extern uint8_t battery_level;

#endif /* BATTERY_READER_H */