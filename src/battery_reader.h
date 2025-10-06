#ifndef BATTERY_READER_H
#define BATTERY_READER_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

/* Battery Service UUID */
#define BT_UUID_BAS_VAL 0x180f
#define BT_UUID_BAS BT_UUID_DECLARE_16(BT_UUID_BAS_VAL)

/* Battery Level Characteristic UUID */
#define BT_UUID_BAS_BATTERY_LEVEL_VAL 0x2a19
#define BT_UUID_BAS_BATTERY_LEVEL BT_UUID_DECLARE_16(BT_UUID_BAS_BATTERY_LEVEL_VAL)

/**
 * @brief Initialize the battery reader module
 * 
 * @return 0 on success, negative error code on failure
 */
int battery_reader_init(void);

/**
 * @brief Discover Battery Service on a connected device
 * 
 * @param conn Pointer to the BLE connection
 * @return 0 on success, negative error code on failure
 */
int battery_discover(struct bt_conn *conn);

/**
 * @brief Read battery level from discovered battery service
 * 
 * @param conn Pointer to the BLE connection
 * @return 0 on success, negative error code on failure
 */
int battery_read_level(struct bt_conn *conn);

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
void battery_reader_reset_state(void);

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