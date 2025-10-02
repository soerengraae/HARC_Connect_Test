#include "ble_manager.h"
#include "vcp_controller.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/uuid.h>

LOG_MODULE_REGISTER(ble_manager, LOG_LEVEL_DBG);

struct bt_conn *connection;
struct bt_conn *auth_conn;

struct deviceInfo
{
	bt_addr_le_t addr;
	char name[BT_NAME_MAX_LEN];
	bool connect;
} scannedDevice;

void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Passkey for %s: %06u", addr, passkey);
}

void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	auth_conn = bt_conn_ref(conn);

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Passkey for %s: %06u", addr, passkey);
	bt_conn_auth_passkey_confirm(auth_conn);
	bt_conn_unref(auth_conn);
	auth_conn = NULL;
}

void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing cancelled: %s", addr);
}

void pairing_complete(struct bt_conn *conn, bool bonded)
{
	LOG_INF("Pairing complete. Bonded: %d", bonded);
	if (!bonded) {
        LOG_ERR("Pairing did not result in bonding!");
        return;
    }

	LOG_INF("Bond established - disconnecting to persist bond");
    bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_ERR("Pairing failed: %d", reason);
}

struct bt_conn_auth_cb auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.cancel = auth_cancel,
};

struct bt_conn_auth_info_cb auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err)
	{
		LOG_DBG("Security changed: %s level %u", addr, level);
	}
	else
	{
		LOG_ERR("Security failed: %s level %u err %d", addr, level, err);
		return;
	}

	if (level >= BT_SECURITY_WANTED)
	{
		LOG_INF("Encryption established - checking if bonded");
            
		// Check if we're bonded (not just encrypted)
		struct bt_conn_info info;
		bt_conn_get_info(conn, &info);
		
		if (info.state == BT_CONN_STATE_CONNECTED && 
			bt_conn_get_security(conn) >= BT_SECURITY_L2) {
			
			// We're encrypted - check if this is a new pairing or existing bond
			// If pairing_complete hasn't been called yet, this is still initial pairing
			// If it has, we can proceed with VCP discovery
			
			LOG_INF("Ready for VCP discovery on bonded connection");
			
			if (!vcp_discovered) {
				k_sleep(K_MSEC(100)); // Small delay for stability
				int vcp_err = vcp_discover(conn);
				if (vcp_err) {
					LOG_ERR("VCP discovery failed (err %d)", vcp_err);
				}
			}
		}
	} else {
		LOG_ERR("Security failed: %s level %u err %d", addr, level, err);
	}
}

/* Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err)
	{
		LOG_ERR("Connection failed (err 0x%02X)", err);
		return;
	}

	LOG_INF("Connected");
	// LOG_DBG("Connection pointer: %p, stored connection: %p", conn, connection);

	LOG_DBG("Requesting pairing with security %d", BT_SECURITY_WANTED);
	int pair_err = bt_conn_set_security(conn, BT_SECURITY_WANTED);
	if (pair_err)
	{
		LOG_ERR("Failed to set security (err %d)", pair_err);
		return;
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason 0x%02X)", reason);

	if (connection)
	{
		LOG_DBG("Unref connection");
		bt_conn_unref(connection);
		connection = NULL;
	}

	LOG_DBG("Restarting scan");
	ble_manager_scan_start();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
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
		LOG_INF("Connecting to %s (RSSI %d)", info.name, rssi);
		bt_le_scan_stop();
		int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &connection);
		if (err)
		{
			LOG_ERR("Create conn to %s failed (err %d)", info.name, err);
		}
	}
}

/* Start BLE scanning */
void ble_manager_scan_start(void)
{
	int err;

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE_CAP_RAP, device_found_cb);
	if (err)
	{
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning for HIs");
}

/* Initialize BLE scanner */
int ble_manager_init(void)
{
	bt_conn_auth_cb_register(&auth_callbacks);
	bt_conn_auth_info_cb_register(&auth_info_callbacks);

	int err = vcp_controller_init();
	if (err)
	{
		LOG_ERR("VCP controller init failed (err %d)", err);
		return err;
	}

	LOG_INF("BLE scanner initialized");
	return 0;
}

void bt_ready(int err)
{
	if (err)
	{
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	// Set fixed passkey for authentication
	// unsigned int passkey = 123456; // Your chosen 6-digit passkey
	// err = bt_passkey_set(passkey);
	// if (err)
	// {
	// 	LOG_ERR("Failed to set passkey (err %d)", err);
	// 	return;
	// }

	/* Initialize BLE manager */
	err = ble_manager_init();
	if (err)
	{
		LOG_ERR("BLE manager init failed (err %d)", err);
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
        err = settings_load();
        if (err) {
            LOG_ERR("Settings load failed (err %d)", err);
        } else {
            LOG_INF("Bonds loaded from storage");
        }
    }

	ble_manager_scan_start();
}