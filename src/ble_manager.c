#include "ble_manager.h"
#include "vcp_controller.h"
#include "battery_reader.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/hci.h>

LOG_MODULE_REGISTER(ble_manager, LOG_LEVEL_INF);

struct bt_conn *auth_conn;
struct connection_context *conn_ctx;
bool first_pairing = true;

void pairing_complete(struct bt_conn *conn, bool bonded)
{
	LOG_DBG("Pairing complete. Bonded: %d", bonded);
	if (!bonded) {
		LOG_ERR("Pairing did not result in bonding!");
		conn_ctx->state = CONN_STATE_DISCONNECTED;
		return;
	}

	conn_ctx->state = CONN_STATE_BONDED;

	if (conn_ctx->info.is_new_device) {
		LOG_INF("New device paired successfully - saving and disconnecting");
		
		// Save the bond
		if (IS_ENABLED(CONFIG_SETTINGS)) {
				settings_save();
		}
		
		// Disconnect to complete initial pairing flow
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	} else {
		// This shouldn't happen for already bonded devices
		LOG_WRN("Unexpected pairing_complete for already bonded device");
	}
}

void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    LOG_ERR("Pairing failed: %d", reason);
    first_pairing = false;

	// Reset scanning
	ble_manager_scan_start();
}

struct bt_conn_auth_info_cb auth_info_callbacks = {
	.pairing_complete = pairing_complete, // This is only called if new bond created, misleading name :(
	.pairing_failed = pairing_failed,
};

void security_changed_cb(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_DBG("Security changed: %s level %u", addr, level);
		
		if (level >= BT_SECURITY_L2) {
			LOG_DBG("Encryption established at level %u", level);

			// Only proceed with VCP if this is a reconnection (not new pairing)
			if (conn_ctx->state == CONN_STATE_BONDED) {
					LOG_DBG("Bonded device encrypted - starting service discovery");
					vcp_discover_start(conn_ctx);
					battery_discover(conn_ctx);
			} else {
					LOG_DBG("New device - waiting for pairing completion");
					conn_ctx->state = CONN_STATE_PAIRING;
			}
		}
	} else {
		LOG_ERR("Security failed: %s level %u err %d", addr, level, err);
		conn_ctx->state = CONN_STATE_DISCONNECTED;
	}
}

static void disconnect(struct bt_conn *conn, void *data)
{
    LOG_INF("Disconnecting connection");
    bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	bt_conn_unref(conn);
	conn = NULL;
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed (err 0x%02X)", err);
		conn_ctx->state = CONN_STATE_DISCONNECTED;
		if (err == -12) // -ENOMEM
		{
			bt_conn_foreach(BT_CONN_TYPE_LE, disconnect, NULL);
		}

		ble_manager_scan_start();
		return;
	}

	LOG_INF("Connected");

	const bt_addr_le_t *addr = bt_conn_get_dst(conn);
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	conn_ctx->conn = bt_conn_ref(conn);
	bt_addr_le_copy(&conn_ctx->info.addr, addr);

	// Check if this device is already bonded
	conn_ctx->info.is_new_device = !is_bonded_device(addr);

	if (conn_ctx->info.is_new_device) {
		LOG_DBG("Connected to new device %s - expecting pairing", addr_str);
		conn_ctx->state = CONN_STATE_PAIRING;
	} else {
		LOG_INF("Connected to bonded device %s", addr_str);
		conn_ctx->state = CONN_STATE_BONDED;
	}
	
	LOG_DBG("Requesting security level %d", BT_SECURITY_WANTED);
	int sec_err = bt_conn_set_security(conn, BT_SECURITY_WANTED);
	if (sec_err) {
		LOG_ERR("Failed to set security (err %d)", sec_err);
	}
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason 0x%02X)", reason);

	if (conn_ctx->conn) {
		bt_conn_unref(conn_ctx->conn);
		conn_ctx->conn = NULL;
	}
	
	vcp_controller_reset_state();

	// In case we just completed first pairing, wait for NVS writes to complete
	// before scanning/reconnecting
	LOG_INF("Waiting for flash writes to complete before restarting scan");
	k_sleep(K_MSEC(1000));

	LOG_INF("Restarting scan");
	ble_manager_scan_start();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.security_changed = security_changed_cb,
};

/* Device discovery function
   Extracts HAS service and device name from advertisement data */
static bool device_found(struct bt_data *data, void *user_data)
{
	struct deviceInfo *info = (struct deviceInfo *)user_data;
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&info->addr, addr_str, sizeof(addr_str));

	LOG_DBG("Advertisement data type 0x%X len %u from %s", data->type, data->data_len, addr_str);

	switch (data->type)
	{
	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_NAME_SHORTENED:
		char name[BT_NAME_MAX_LEN];
		memset(name, 0, sizeof(name));
		strncpy(name, (char *)data->data, MIN(data->data_len, BT_NAME_MAX_LEN - 1));

		LOG_DBG("Found device name: %.*s", data->data_len, (char *)data->data);
		int cmp = strcmp(name, "HARC HI");
		LOG_DBG("strcmp result: %d", cmp);
		if (!cmp)
		{
			strncpy(info->name, name, BT_NAME_MAX_LEN - 1);
			info->name[BT_NAME_MAX_LEN - 1] = '\0'; // Ensure null-termination
			info->connect = true;
			LOG_DBG("Will attempt to connect to %s", info->name);
			return false; // Stop parsing further
		}

		break;

	default:
		return true;
	}

	return true;
}

static uint8_t connect(struct deviceInfo info) {
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&info.addr, addr_str, sizeof(addr_str));
	LOG_INF("Connecting to %s (with address %s)", info.name, addr_str);

	bt_le_scan_stop();
	conn_ctx->state = CONN_STATE_CONNECTING;

	// BT_CONN_LE_CREATE_CONN uses 100% duty cycle
	int err = bt_conn_le_create(&info.addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &conn_ctx->conn);
	if (err)
	{
		LOG_ERR("Create conn to %s failed (err %d)", info.name, err);
		if (err == -12) // -ENOMEM
		{
			bt_conn_foreach(BT_CONN_TYPE_LE, disconnect, NULL);
		}

		return err; // Will restart scan
	}

	return 0;
}

static void device_found_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	struct deviceInfo info = {0};
	info.addr = *addr;
	info.connect = false;

	bt_data_parse(ad, device_found, &info);

	if (info.connect)
	{
		if (connect(info)) {
			LOG_DBG("Restarting scan");
			ble_manager_scan_start();
		}
	}
}

/* Start BLE scanning */
void ble_manager_scan_start(void)
{
	int err;
	err = bt_le_scan_stop();
	if (err && err != -EALREADY)
	{
		LOG_ERR("Stopping existing scan failed (err %d)", err);
		return;
	}

	bt_conn_unref(conn_ctx->conn);
	conn_ctx->conn = NULL;

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE_CAP_RAP, device_found_cb);
	if (err)
	{
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning for HIs");
}

/** Initialize BLE manager
 * @brief Sets up connection callbacks, authentication, VCP controller, and battery reader
 * 
 * @return 0 on success, negative error code on failure
 */
int ble_manager_init(void)
{
	// bt_conn_auth_cb_register(&auth_callbacks);
	bt_conn_auth_info_cb_register(&auth_info_callbacks);

	int err = vcp_controller_init();
	if (err)
	{
		LOG_ERR("VCP controller init failed (err %d)", err);
		return err;
	}

	conn_ctx = (struct connection_context *)k_calloc(1, sizeof(struct connection_context));
	if (!conn_ctx) {
		LOG_ERR("Failed to allocate memory for connection context");
		return -ENOMEM;
	}

	LOG_INF("BLE manager initialized");
	return 0;
}

void is_bonded_device_cb(const struct bt_bond_info *info, void *user_data) {
	struct deviceInfo *data = (struct deviceInfo *)user_data;
	bt_addr_le_copy(&data->addr, &info->addr);
	data->connect = true;
}

void bt_ready_cb(int err)
{
	if (err)
	{
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	/* Initialize BLE manager */
	err = ble_manager_init();
	if (err)
	{
		LOG_ERR("BLE manager init failed (err %d)", err);
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		// Get address of first bonded device
		settings_load_subtree("bt/bond");
    }

	bt_foreach_bond(BT_ID_DEFAULT, is_bonded_device_cb, &conn_ctx->info);

	if (conn_ctx->info.connect) {
		LOG_INF("Connecting to previously bonded device");
		int err = bt_le_set_auto_conn(&conn_ctx->info.addr, BT_LE_CONN_PARAM_DEFAULT);
		if (err) {
			LOG_ERR("Failed to connect to bonded device (err %d)", err);
			ble_manager_scan_start();
		}
	} else {
		LOG_INF("No previously bonded device found");
		ble_manager_scan_start();
	}
}

bool is_bonded_device(const bt_addr_le_t *addr)
{
    struct check_bonded_data {
        const bt_addr_le_t *addr;
        bool found;
    } check_data = {
        .addr = addr,
        .found = false
    };
    
    bt_foreach_bond(BT_ID_DEFAULT, is_bonded_device_cb, &check_data);
    
    return check_data.found;
}