#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/settings/settings.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief Helper macro for fulfilling scanning parameters
 * according to the CAP Connection procedures "Ready for Audio related Peripheral" mode.
 * 
 * This will save power when compared to the default @ref BT_LE_SCAN_ACTIVE
 */
#define BT_LE_SCAN_ACTIVE_CAP_RAP BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_ACTIVE, \
            BT_LE_SCAN_OPT_FILTER_DUPLICATE, \
            BT_GAP_SCAN_SLOW_INTERVAL_1, \
            BT_GAP_SCAN_SLOW_WINDOW_1)

#define MAX_DISCOVERED_DEVICES_MEMORY_SIZE 1024 // 1 KB
#define BT_NAME_MAX_LEN 12
#define BT_SECURITY_WANTED BT_SECURITY_L2

struct deviceInfo
{
	bt_addr_le_t addr;
	char name[BT_NAME_MAX_LEN];
	bool connect;
};

/* BLE scanner functions */
int ble_manager_init(void);
void ble_manager_scan_start(void);
void bt_ready(int err);
void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err);
void auth_passkey_display(struct bt_conn *conn, unsigned int passkey);
void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey);
void auth_cancel(struct bt_conn *conn);
void pairing_complete(struct bt_conn *conn, bool bonded);
void pairing_failed(struct bt_conn *conn, enum bt_security_err reason);

/* Connection management */
extern struct bt_conn_cb conn_callbacks;
extern struct bt_conn *conn;
extern struct bt_conn *auth_conn;

/* Global state */
extern bool first_pairing;
extern struct deviceInfo scannedDevice;

#endif /* BLE_MANAGER_H */