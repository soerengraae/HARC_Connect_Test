#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/byteorder.h>
#include <stdint.h>
#include <string.h>


/**
 * @brief Helper macro for fulfilling scanning parameters
 * according to the CAP Connection procedures "Ready for Audio related Peripheral" mode.
 * 
 * This will save power when compared to the default @ref BT_LE_SCAN_ACTIVE.
 * It's only used when scanning for a bondable device. If a device is already bonded, we use 100% duty cycle to scan and connect.
 */
#define BT_LE_SCAN_ACTIVE_CAP_RAP BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_ACTIVE, \
            BT_LE_SCAN_OPT_NONE, \
            BT_GAP_SCAN_SLOW_INTERVAL_1, \
            BT_GAP_SCAN_SLOW_WINDOW_1)

#define MAX_DISCOVERED_DEVICES_MEMORY_SIZE 1024 // 1 KB
#define BT_NAME_MAX_LEN 12
#define BT_SECURITY_WANTED BT_SECURITY_L2
#define BT_DEVICE_READY_TIMEOUT_MS 30000
#define BT_SCAN_TIMEOUT_MS 60000
#define BT_AUTO_CONNECT_TIMEOUT_MS 4000

/* CSIP Set Information */
#define CSIP_SIRK_SIZE 16

struct bt_bas_ctlr {
    uint16_t battery_service_handle;
    uint16_t battery_service_handle_end;
    uint16_t battery_level_handle;
    uint16_t battery_level_ccc_handle;
    uint8_t battery_level;
};

/* BLE command types */
enum ble_cmd_type {
    BLE_CMD_REQUEST_SECURITY,

    /* VCP commands */
    BLE_CMD_VCP_DISCOVER,
    BLE_CMD_VCP_VOLUME_UP,
    BLE_CMD_VCP_VOLUME_DOWN,
    BLE_CMD_VCP_SET_VOLUME,
    BLE_CMD_VCP_MUTE,
    BLE_CMD_VCP_UNMUTE,
    BLE_CMD_VCP_READ_STATE,
    BLE_CMD_VCP_READ_FLAGS,

    /* Battery Service commands */
    BLE_CMD_BAS_DISCOVER,
    BLE_CMD_BAS_READ_LEVEL,

    /* CSIP commands */
    BLE_CMD_CSIP_DISCOVER,
    
    /* Hearing Access Service commands */
    BLE_CMD_HAS_DISCOVER,
    BLE_CMD_HAS_READ_PRESETS,
    BLE_CMD_HAS_SET_PRESET,
    BLE_CMD_HAS_NEXT_PRESET,
    BLE_CMD_HAS_PREV_PRESET,
};

/* BLE command structure */
struct ble_cmd {
    uint8_t device_id;
    enum ble_cmd_type type;
    uint8_t d0;  // Data parameter (e.g., volume level)
    uint8_t retry_count;
    sys_snode_t node;  // For linked list
};

/* Command queue configuration */
#define BLE_CMD_QUEUE_SIZE 5
#define BLE_CMD_TIMEOUT_MS 5000

/* BLE manager public functions */
int ble_manager_init(void);
void bt_ready_cb(int err);
void ble_manager_set_device_ctx_battery_level(struct bt_conn *conn, uint8_t level);
void ble_manager_start_scan_for_HIs(void);
void ble_manager_stop_scan_for_HIs(void);
int ble_manager_autoconnect_to_bonded_device(uint8_t device_id);
int ble_manager_autoconnect_to_device_by_addr(const bt_addr_le_t *addr);
int ble_manager_connect_to_scanned_device(uint8_t device_id, uint8_t idx);
void ble_manager_establish_trusted_bond(uint8_t device_id);


/* BLE command queue API */
int ble_cmd_request_security(uint8_t device_id);
int ble_cmd_vcp_discover(uint8_t device_id, bool high_priority);
int ble_cmd_vcp_volume_up(uint8_t device_id, bool high_priority);
int ble_cmd_vcp_volume_down(uint8_t device_id, bool high_priority);
int ble_cmd_vcp_set_volume(uint8_t device_id, uint8_t volume, bool high_priority);
int ble_cmd_vcp_mute(uint8_t device_id, bool high_priority);
int ble_cmd_vcp_unmute(uint8_t device_id, bool high_priority);
int ble_cmd_vcp_read_state(uint8_t device_id, bool high_priority);
int ble_cmd_vcp_read_flags(uint8_t device_id, bool high_priority);

int ble_cmd_bas_discover(uint8_t device_id, bool high_priority);
int ble_cmd_bas_read_level(uint8_t device_id, bool high_priority);

int ble_cmd_csip_discover(uint8_t device_id, bool high_priority);

void ble_cmd_queue_reset(uint8_t queue_id);

void ble_cmd_complete(uint8_t device_id, int err);

/* Connection management */
extern struct bt_conn_cb conn_callbacks;
extern struct bt_conn *auth_conn;
int ble_manager_disconnect_device(struct bt_conn *conn);

/* Connection initiation */
int schedule_auto_connect(uint8_t device_id);

/* HAS command queue API */
int ble_cmd_has_discover(bool high_priority);
int ble_cmd_has_read_presets(bool high_priority);
int ble_cmd_has_set_preset(uint8_t preset_index, bool high_priority);
int ble_cmd_has_next_preset(bool high_priority);
int ble_cmd_has_prev_preset(bool high_priority);

#endif /* BLE_MANAGER_H */