#include "ble_manager.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/audio/vcp.h>

LOG_MODULE_REGISTER(ble_manager, LOG_LEVEL_DBG);

struct bt_conn *connection;

struct deviceInfo {
	bt_addr_le_t addr;
	char name[BT_NAME_MAX_LEN];
	bool connect;
} scannedDevice;

/* Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err)
	{
		LOG_ERR("Connection failed (err 0x%02X)", err);
		return;
	}

	LOG_INF("Connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_DBG("Disconnected (reason 0x%02X)", reason);

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
	LOG_INF("BLE scanner initialized");
	return 0;
}

void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	/* Initialize BLE manager */
	err = ble_manager_init();
	if (err) {
		LOG_ERR("BLE manager init failed (err %d)", err);
		return;
	}

	ble_manager_scan_start();
}