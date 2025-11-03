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
 * This will save power when compared to the default @ref BT_LE_SCAN_ACTIVE.
 * It's only used when scanning for a bondable device. If a device is already bonded, we use 100% duty cycle to scan and connect.
 */
#define BT_LE_SCAN_ACTIVE_CAP_RAP BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_ACTIVE, \
            BT_LE_SCAN_OPT_FILTER_DUPLICATE, \
            BT_GAP_SCAN_SLOW_INTERVAL_1, \
            BT_GAP_SCAN_SLOW_WINDOW_1)

#define MAX_DISCOVERED_DEVICES_MEMORY_SIZE 1024 // 1 KB
#define BT_NAME_MAX_LEN 12
#define BT_SECURITY_WANTED BT_SECURITY_L2

/* CSIP Set Information */
#define CSIP_SIRK_SIZE 16

struct bonded_device_entry {
    bt_addr_le_t addr;
    char name[BT_NAME_MAX_LEN];
    uint8_t sirk[CSIP_SIRK_SIZE];
    bool has_sirk;
    uint8_t set_rank;  // 1 = left, 2 = right
    uint32_t last_connected_timestamp;
    bool is_set_member;
};

struct bond_collection {
    struct bonded_device_entry devices[CONFIG_BT_MAX_PAIRED];
    uint8_t count;
};

struct device_info
{
	bt_addr_le_t addr;
	char name[BT_NAME_MAX_LEN];
	bool connect;
    bool is_new_device;  // True if not previously bonded
    bool vcp_discovered;
    bool bas_discovered;
    bool csip_discovered;
};

enum connection_state {
    CONN_STATE_DISCONNECTED,
    CONN_STATE_CONNECTING,
    CONN_STATE_PAIRING,
    CONN_STATE_BONDED
};

struct bt_bas_ctlr {
    uint16_t battery_service_handle;
    uint16_t battery_service_handle_end;
    uint16_t battery_level_handle;
    uint16_t battery_level_ccc_handle;
    uint8_t battery_level;
};

struct device_context {
    uint8_t device_id;
    struct bt_conn *conn;
    enum connection_state state;
    struct device_info info;
    struct bt_vcp_vol_ctlr *vol_ctlr;
    struct bt_bas_ctlr bas_ctlr;
    struct ble_cmd *current_ble_cmd;
};

/* BLE command types */
enum ble_cmd_type {
    BLE_CMD_REQUEST_SECURITY,

    /* Volume Control commands */
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

    /* Coordinated Sets Identification commands */
    BLE_CMD_CSIP_DISCOVER,
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
struct device_context *get_device_context_by_conn(struct bt_conn *conn);
struct device_context *get_device_context_by_id(uint8_t device_id);

/* BLE command queue API */
int ble_cmd_request_security(uint8_t select_device);
int ble_cmd_vcp_discover(uint8_t select_device, bool high_priority);
int ble_cmd_vcp_volume_up(uint8_t select_device, bool high_priority);
int ble_cmd_vcp_volume_down(uint8_t select_device, bool high_priority);
int ble_cmd_vcp_set_volume(uint8_t select_device, uint8_t volume, bool high_priority);
int ble_cmd_vcp_mute(uint8_t select_device, bool high_priority);
int ble_cmd_vcp_unmute(uint8_t select_device, bool high_priority);
int ble_cmd_vcp_read_state(uint8_t select_device, bool high_priority);
int ble_cmd_vcp_read_flags(uint8_t select_device, bool high_priority);

int ble_cmd_bas_discover(uint8_t select_device, bool high_priority);
int ble_cmd_bas_read_level(uint8_t select_device, bool high_priority);

void ble_cmd_queue_reset(uint8_t queue_id);

/* Command completion notification from subsystems */
void ble_cmd_complete(uint8_t device_id, int err);

/* Connection management */
extern struct bt_conn_cb conn_callbacks;
extern struct bt_conn *auth_conn;
extern struct device_context *device_ctx;

/* Bond enumeration and management */
int enumerate_bonded_devices(struct bond_collection *collection);

#endif /* BLE_MANAGER_H */