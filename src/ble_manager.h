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

struct deviceInfo
{
	bt_addr_le_t addr;
	char name[BT_NAME_MAX_LEN];
	bool connect;
    bool is_new_device;  // True if not previously bonded
};

enum connection_state {
    CONN_STATE_DISCONNECTED,
    CONN_STATE_CONNECTING,
    CONN_STATE_PAIRING,
    CONN_STATE_BONDED
};

struct connection_context {
    struct bt_conn *conn;
    enum connection_state state;
    struct deviceInfo info;
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
};

/* BLE command structure */
struct ble_cmd {
    struct bt_conn *conn;
    enum ble_cmd_type type;
    uint8_t d0;  // Data parameter (e.g., volume level)
    uint8_t retry_count;
    sys_snode_t node;  // For linked list
};

extern struct ble_cmd *current_ble_cmd;

/* Command queue configuration */
#define BLE_CMD_QUEUE_SIZE 10
#define BLE_CMD_TIMEOUT_MS 5000

/* BLE scanner functions */
int ble_manager_init(void);
int connect_to_bonded_device(void);
void scan_for_HIs(void);
void bt_ready_cb(int err);
bool is_bonded_device(const bt_addr_le_t *addr);
void disconnect(struct bt_conn *conn, void *data); // void *data ensures compatibility with bt_conn_foreach

char *command_type_to_string(enum ble_cmd_type type);

/* BLE command queue API */
int ble_cmd_request_security(void);
int ble_cmd_vcp_discover(bool high_priority);
int ble_cmd_vcp_volume_up(bool high_priority);
int ble_cmd_vcp_volume_down(bool high_priority);
int ble_cmd_vcp_set_volume(uint8_t volume, bool high_priority);
int ble_cmd_vcp_mute(bool high_priority);
int ble_cmd_vcp_unmute(bool high_priority);
int ble_cmd_vcp_read_state(bool high_priority);
int ble_cmd_vcp_read_flags(bool high_priority);

int ble_cmd_bas_discover(bool high_priority);
int ble_cmd_bas_read_level(bool high_priority);

void ble_cmd_queue_reset(void);

/* Command completion notification from subsystems */
void ble_cmd_complete(int err);

/* Connection management */
extern struct bt_conn_cb conn_callbacks;
extern struct bt_conn *auth_conn;
extern struct connection_context *current_conn_ctx;

#endif /* BLE_MANAGER_H */