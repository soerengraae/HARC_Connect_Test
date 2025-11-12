#include "ble_manager.h"
#include "vcp_controller.h"
#include "battery_reader.h"
#include "csip_coordinator.h"
#include "app_controller.h"
#include "devices_manager.h"
#include "display_manager.h"
#include "has_controller.h"

LOG_MODULE_REGISTER(ble_manager, LOG_LEVEL_DBG);

static struct bond_collection *bonded_devices;
static struct k_work_delayable auto_connect_work[2];
static struct k_work_delayable auto_connect_timeout_work[2];
static struct k_work_delayable security_request_work[2];

/* BLE Command queue state */
static sys_slist_t ble_cmd_queue[2];
static struct k_mutex ble_queue_mutex[2];
static struct k_sem ble_cmd_sem[2];
static struct k_work_delayable ble_cmd_timeout_work[2];
static bool ble_cmd_in_progress[2] = {false, false};
// static bool queue_is_active[2] = {false, false};

/* Memory pool for BLE commands */
K_MEM_SLAB_DEFINE(ble_cmd_slab_0, sizeof(struct ble_cmd), BLE_CMD_QUEUE_SIZE, 4);
K_MEM_SLAB_DEFINE(ble_cmd_slab_1, sizeof(struct ble_cmd), BLE_CMD_QUEUE_SIZE, 4);

/* Forward declarations */
static void ble_process_next_command(uint8_t queue_id);
static void ble_cmd_timeout_handler(struct k_work *work);
static uint8_t connect(struct device_context *ctx);
// static bool is_bonded_device(const bt_addr_le_t *addr);
static char *command_type_to_string(enum ble_cmd_type type);

// static void activate_ble_cmd_queue(uint8_t device_id)
// {
// 	if (!queue_is_active[device_id])
// 	{
// 		queue_is_active[device_id] = true;
// 		LOG_DBG("Activating BLE command queue [DEVICE ID %d]", device_id);
// 		k_sem_give(&ble_cmd_sem[device_id]);
// 	}
// }

/* Command queue initialization */
static int ble_queues_init(void)
{
	for (ssize_t i = 0; i < 2; i++)
	{
		sys_slist_init(&ble_cmd_queue[i]);
		k_mutex_init(&ble_queue_mutex[i]);
		k_sem_init(&ble_cmd_sem[i], 0, 1);
		k_work_init_delayable(&ble_cmd_timeout_work[i], ble_cmd_timeout_handler);
		device_ctx[i].current_ble_cmd = NULL;
	}

	return 0;
}

/* Allocate a command from memory pool */
static struct ble_cmd *ble_cmd_alloc(uint8_t device_id)
{
	struct ble_cmd *cmd;
	struct k_mem_slab *slab = (device_id == 0) ? &ble_cmd_slab_0 : &ble_cmd_slab_1;

	if (k_mem_slab_alloc(slab, (void **)&cmd, K_NO_WAIT) != 0)
	{
		LOG_ERR("Failed to allocate BLE command - queue full [DEVICE ID %d]", device_id);
		return NULL;
	}

	memset(cmd, 0, sizeof(struct ble_cmd));
	return cmd;
}

/* Free a command back to memory pool */
static void ble_cmd_free(struct ble_cmd *cmd)
{
	if (cmd)
	{
		struct k_mem_slab *slab = (cmd->device_id == 0) ? &ble_cmd_slab_0 : &ble_cmd_slab_1;
		k_mem_slab_free(slab, (void *)cmd);
	}
}

/* Enqueue a command */
static int ble_cmd_enqueue(struct ble_cmd *cmd, bool high_priority)
{
	if (!cmd)
	{
		return -EINVAL;
	}

	k_mutex_lock(&ble_queue_mutex[cmd->device_id], K_FOREVER);
	if (high_priority)
	{
		sys_slist_prepend(&ble_cmd_queue[cmd->device_id], &cmd->node);
	}
	else
	{
		sys_slist_append(&ble_cmd_queue[cmd->device_id], &cmd->node);
	}
	k_mutex_unlock(&ble_queue_mutex[cmd->device_id]);

	// Signal the processing thread
	k_sem_give(&ble_cmd_sem[cmd->device_id]);

	LOG_DBG("%sBLE command enqueued, type: %s [DEVICE ID %d]", high_priority ? "High priority " : "", command_type_to_string(cmd->type), cmd->device_id);
	return 0;
}

/* Dequeue a command */
static struct ble_cmd *ble_cmd_dequeue(uint8_t device_id)
{
	struct ble_cmd *cmd = NULL;

	k_mutex_lock(&ble_queue_mutex[device_id], K_FOREVER);
	sys_snode_t *node = sys_slist_get(&ble_cmd_queue[device_id]);
	if (node)
	{
		cmd = CONTAINER_OF(node, struct ble_cmd, node);
	}
	k_mutex_unlock(&ble_queue_mutex[device_id]);

	return cmd;
}

static void security_request_handler(struct k_work *work)
{
	uint8_t device_id = (work == &security_request_work[0].work) ? 0 : 1;
	struct device_context *ctx = &device_ctx[device_id];

	LOG_DBG("Requesting security [DEVICE ID %d]", device_id);
	int err = bt_conn_set_security(device_ctx[device_id].conn, BT_SECURITY_WANTED);
	if (err)
	{
		LOG_ERR("Failed to set security (err %d) [DEVICE ID %d]", err, device_id);
		ble_cmd_complete(device_id, err);
		return;
	}
	LOG_DBG("Security request initiated [DEVICE ID %d]", device_id);
	
	if (ctx->state == CONN_STATE_CONNECTED)
		ctx->state = CONN_STATE_PAIRING;
}

void pairing_complete(struct bt_conn *conn, bool bonded)
{
	struct device_context *ctx = devices_manager_get_device_context_by_conn(conn);

	LOG_DBG("Pairing complete. Bonded: %d [DEVICE ID %d]", bonded, ctx->device_id);
	if (!bonded)
	{
		LOG_ERR("Pairing did not result in bonding! [DEVICE ID %d]", ctx->device_id);
		ctx->state = CONN_STATE_DISCONNECTED;
		return;
	}

	ctx->state = CONN_STATE_PAIRED;

	if (ctx->info.is_new_device)
	{
		LOG_INF("New device paired successfully - saving bond [DEVICE ID %d]", ctx->device_id);
		// Save the bond
		if (IS_ENABLED(CONFIG_SETTINGS))
		{
			LOG_DBG("Saving bond information to flash [DEVICE ID %d]", ctx->device_id);
			settings_save();
		}

		devices_manager_update_bonded_devices_collection();
		devices_manager_get_bonded_devices_collection(bonded_devices);
	}
	else
	{
		// This shouldn't happen for already bonded devices
		LOG_WRN("Unexpected pairing_complete for already bonded device [DEVICE ID %d]", ctx->device_id);
	}

	app_controller_notify_device_ready(ctx->device_id);
}

void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	struct device_context *ctx = devices_manager_get_device_context_by_conn(conn);
	LOG_ERR("Pairing failed: %d [DEVICE ID %d]", reason, ctx->device_id);
	ble_manager_disconnect_device(conn);
}

struct bt_conn_auth_info_cb auth_info_callbacks = {
	.pairing_complete = pairing_complete, // This is only called if new bond created
	.pairing_failed = pairing_failed,	  // Same for this - if it fails during new bond
};

void security_changed_cb(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	struct device_context *ctx = devices_manager_get_device_context_by_conn(conn);

	if (!err)
	{
		LOG_DBG("Security changed: %s level %u [DEVICE ID %d]", addr, level, ctx->device_id);

		if (level >= BT_SECURITY_L2)
		{
			LOG_DBG("Encryption established at level %u [DEVICE ID %d]", level, ctx->device_id);

			if (ctx->state == CONN_STATE_BONDED)
			{
				LOG_DBG("Bonded device - encryption established [DEVICE ID %d]", ctx->device_id);
				app_controller_notify_device_ready(ctx->device_id);
			}
			else if (ctx->state == CONN_STATE_PAIRING)
			{
				LOG_DBG("New device - waiting for pairing completion [DEVICE ID %d]", ctx->device_id);
			}
			else
			{
				LOG_ERR("Unexpected security change state %d [DEVICE ID %d]", ctx->state, ctx->device_id);
			}
		}
	}
	else
	{
		LOG_ERR("Security failed: %s level %u err %d [DEVICE ID %d]", addr, level, err, ctx->device_id);
	}

	ble_cmd_complete(ctx->device_id, err);
}

void ble_manager_establish_trusted_bond(uint8_t device_id)
{
	struct device_context *ctx = &device_ctx[device_id];
	LOG_INF("Establishing trusted bond with device [DEVICE ID %d]", device_id);
	ctx->state = CONN_STATE_BONDED;

	int err = ble_manager_disconnect_device(ctx->conn);
	if (err)
	{
		LOG_WRN("Failed to disconnect for bonding (err %d) [DEVICE ID %d]", err, device_id);

		if (err == 1) {
			LOG_DBG("Scheduling connection to establish bond [DEVICE ID %d]", ctx->device_id);
			ble_manager_autoconnect_to_bonded_device(ctx->device_id);
		}
	}
}
	

int ble_manager_disconnect_device(struct bt_conn *conn)
{
	struct device_context *ctx = devices_manager_get_device_context_by_conn(conn);
	if (!ctx)
	{
		LOG_ERR("Cannot disconnect - device context not found");
		return -EINVAL;
	}

	LOG_INF("Disconnecting connection [DEVICE ID %d]", ctx->device_id);
	int err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (err != 0) {
		LOG_WRN("Failed to initiate disconnection [DEVICE ID %d]", ctx->device_id);
		if (err == -ENOTCONN) {
			LOG_DBG("Device already disconnected [DEVICE ID %d]", ctx->device_id);
			return 1;
		}
		return err;
	}

	// bt_conn_unref(conn);
	return 0;
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	struct device_context *ctx = devices_manager_get_device_context_by_conn(conn);
	if (!ctx)
	{
		LOG_DBG("Using first slot for new connection");
		ctx = &device_ctx[0];
	}

	if (err)
	{
		LOG_ERR("Connection failed (err 0x%02X)", err);
		ctx->state = CONN_STATE_DISCONNECTED;

		// Cancel auto-connect if it was active
		bt_conn_create_auto_stop();
		k_work_cancel_delayable(&auto_connect_timeout_work[ctx->device_id]);
		return;
	}

	// Cancel the timeout since we connected successfully
	k_work_cancel_delayable(&auto_connect_timeout_work[ctx->device_id]);

	const bt_addr_le_t *addr = bt_conn_get_dst(conn);
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	ctx->conn = bt_conn_ref(conn);
	bt_addr_le_copy(&ctx->info.addr, addr);

	if (ctx->state == CONN_STATE_CONNECTING)
	{
		LOG_DBG("Connected to new device %s - expecting pairing [DEVICE ID %d]", addr_str, ctx->device_id);
		ctx->state = CONN_STATE_CONNECTED;
	}
	else
	{
		LOG_INF("Connected to bonded (or bonding) device %s [DEVICE ID %d]", addr_str, ctx->device_id);
	}

	// queue_is_active[ctx->device_id] = true;
	ble_cmd_request_security(ctx->device_id);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	struct device_context *ctx = devices_manager_get_device_context_by_conn(conn);
	char addr_str0[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str0, sizeof(addr_str0));
	LOG_INF("Disconnected from %s with (reason 0x%02X) [DEVICE ID %d]", addr_str0, reason, ctx->device_id);

	bt_conn_unref(ctx->conn);
	ctx->conn = NULL;

	// if (queue_is_active[ctx->device_id])
	// 	ble_cmd_queue_reset(ctx->device_id);

	if (ctx->info.vcp_discovered)
		vcp_controller_reset(ctx->device_id);
	if (ctx->info.bas_discovered)
		battery_reader_reset(ctx->device_id);

	/**
	 * If the disconnection was intentional (local host terminated),
	 * we will decide whether to attempt to reconnect.
	 * If the disconnection was unintentional, we simply set the state
	 * to DISCONNECTED and wait for further instructions.
	 */
	if (reason != BT_HCI_ERR_LOCALHOST_TERM_CONN && reason != BT_HCI_ERR_CONN_FAIL_TO_ESTAB) {
		ctx->state = CONN_STATE_DISCONNECTED;
		return;
	}

	if (ctx->state == CONN_STATE_PAIRING)
	{
		LOG_WRN("Disconnected during pairing process [DEVICE ID %d]", ctx->device_id);
		LOG_DBG("Scheduling reconnection to complete pairing [DEVICE ID %d]", ctx->device_id);
		struct scanned_device_entry *scanned_device = devices_manager_get_scanned_device(0);
		if (!scanned_device)
		{
			LOG_ERR("Device not found in scanned devices list, cannot reconnect [DEVICE ID %d]", ctx->device_id);
			return;
		}

		if (scanned_device->addr_count < 2)
		{
			LOG_WRN("Not enough addresses in scanned device entry to switch address for reconnection [DEVICE ID %d]", ctx->device_id);
			LOG_DBG("Scheduling reconnection with same address [DEVICE ID %d]", ctx->device_id);
		}
		else
		{
			LOG_DBG("Scanned device has %d addresses", scanned_device->addr_count);
			
			char addr_str1[BT_ADDR_LE_STR_LEN];
			if (bt_addr_le_cmp(&ctx->info.addr, &scanned_device->addrs[0]) == 0)
			{
				bt_addr_le_to_str(&ctx->info.addr, addr_str0, sizeof(addr_str0));
				bt_addr_le_to_str(&scanned_device->addrs[0], addr_str1, sizeof(addr_str1));

				LOG_DBG("Compared %s to %s",
						addr_str0,
						addr_str1);
				LOG_DBG("Same address, switching to second address for reconnection [DEVICE ID %d]", ctx->device_id);
				bt_addr_le_copy(&ctx->info.addr, &scanned_device->addrs[1]);
			}
			else if (bt_addr_le_cmp(&ctx->info.addr, &scanned_device->addrs[1]) == 0)
			{
				bt_addr_le_to_str(&ctx->info.addr, addr_str0, sizeof(addr_str0));
				bt_addr_le_to_str(&scanned_device->addrs[1], addr_str1, sizeof(addr_str1));
				LOG_DBG("Comparing %s to %s",
						addr_str0,
						addr_str1);
				LOG_DBG("Same address, switching to first address for reconnection [DEVICE ID %d]", ctx->device_id);
				bt_addr_le_copy(&ctx->info.addr, &scanned_device->addrs[0]);
			}
			else
			{
				LOG_ERR("Current address not found in scanned device addresses, cannot switch [DEVICE ID %d]", ctx->device_id);
				return;
			}
		}

		ble_manager_autoconnect_to_device_by_addr(&ctx->info.addr);
		return;
	} else if (ctx->state == CONN_STATE_BONDED) {
		LOG_INF("Disconnected to establish trusted bond [DEVICE ID %d]", ctx->device_id);
		LOG_DBG("Scheduling reconnection to establish bond [DEVICE ID %d]", ctx->device_id);
		ble_manager_autoconnect_to_bonded_device(ctx->device_id);
		return;
	} else {
		LOG_WRN("Disconnected unexpectedly, state = %d [DEVICE ID %d]", ctx->state, ctx->device_id);
		ctx->state = CONN_STATE_DISCONNECTED;
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.security_changed = security_changed_cb,
};

/* Struct to hold scan callback user data */
struct scan_callback_data
{
	bt_addr_le_t addr;
	int8_t rssi;
	bool has_service_uuid;
};

/* Device discovery function
   Extracts device name and service UUID from advertisement data */
static bool device_found(struct bt_data *data, void *user_data)
{
	struct scan_callback_data *scan_data = (struct scan_callback_data *)user_data;
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&scan_data->addr, addr_str, sizeof(addr_str));

	// LOG_DBG("Advertisement data type 0x%X len %u from %s", data->type, data->data_len,
	// 		addr_str);

	switch (data->type)
	{
	case BT_DATA_SVC_DATA16:
		if (data->data_len >= 2)
		{
			for (size_t i = 0; i <= data->data_len - 2; i += 2)
			{
				uint16_t uuid_val = sys_get_le16(&data->data[i]);
				// LOG_DBG("Service Data UUID 0x%04X", uuid_val);

				if (uuid_val == 0xFEFE)
				{
					LOG_DBG("Found GN Hearing HI service UUID");
					scan_data->has_service_uuid = true;
					devices_manager_add_scanned_device(&scan_data->addr, scan_data->rssi);
				}
			}
		}
		break;

	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_NAME_SHORTENED:
		char name[BT_NAME_MAX_LEN];
		memset(name, 0, sizeof(name));
		strncpy(name, (char *)data->data, MIN(data->data_len, BT_NAME_MAX_LEN - 1));

		LOG_DBG("Found device name: %.*s", data->data_len, (char *)data->data);

		// Update name in scanned devices list if device is already added
		devices_manager_update_scanned_device_name(&scan_data->addr, name);
		break;

	default:
		return true;
	}

	return true;
}

static uint8_t connect(struct device_context *ctx)
{
	if (ctx->conn != NULL)
	{
		LOG_ERR("Connection already exists [DEVICE ID %d]", ctx->device_id);
		return EALREADY;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&ctx->info.addr, addr_str, sizeof(addr_str));
	LOG_INF("Connecting to %s [DEVICE ID %d]", addr_str, ctx->device_id);

	ctx->state = CONN_STATE_CONNECTING;

	// BT_CONN_LE_CREATE_CONN uses 100% duty cycle
	int err = bt_conn_le_create(&ctx->info.addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &ctx->conn);
	if (err)
	{
		LOG_ERR("Create conn to %s failed (err %d) [DEVICE ID %d]", addr_str, err, ctx->device_id);

		return err;
	}

	return 0;
}

/**
 * @brief Connect to a device by index in scanned devices list
 *
 * @param device_id Device ID (0 or 1)
 * @param idx Index in scanned devices list
 * @return 0 on success, negative error code on failure
 */
int ble_manager_connect_to_scanned_device(uint8_t device_id, uint8_t idx)
{
	// Use first available device context
	struct device_context *ctx = &device_ctx[device_id];

	struct scanned_device_entry *scanned_device = devices_manager_get_scanned_device(idx);
	if (!scanned_device)
	{
		LOG_ERR("Invalid scanned device index %d", idx);
		return -EINVAL;
	}

	if (scanned_device->addr_count == 0)
	{
		LOG_ERR("Scanned device has no addresses");
		return -EINVAL;
	}

	// Populate device info (use first address)
	bt_addr_le_copy(&ctx->info.addr, &scanned_device->addrs[0]);
	ctx->info.is_new_device = true; // Assuming new device for scanned devices
	ctx->state = CONN_STATE_DISCONNECTED;
	return ble_manager_autoconnect_to_device_by_addr(&ctx->info.addr);
}

static void device_found_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
							struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	struct scan_callback_data scan_data = {0};
	scan_data.addr = *addr;
	scan_data.rssi = rssi;
	scan_data.has_service_uuid = false;

	// Parse advertisement data to find service UUID and name
	bt_data_parse(ad, device_found, &scan_data);
}

/* Start BLE scanning */
void ble_manager_start_scan_for_HIs(void)
{
	int err;
	err = bt_le_scan_stop();
	if (err)
	{
		LOG_ERR("Stopping existing scan failed (err %d)", err);
		return;
	}

	// Clear any previous scanned devices
	devices_manager_clear_scanned_devices();

	// bt_conn_unref(device_ctx->conn);
	// device_ctx->conn = NULL;

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE_CAP_RAP, device_found_cb);
	if (err)
	{
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning for HIs");
}

void ble_manager_stop_scan_for_HIs(void)
{
	int err = bt_le_scan_stop();
	if (err)
	{
		LOG_ERR("Stopping scan failed (err %d)", err);
		return;
	}

	LOG_INF("Scan stopped");
}

static void auto_connect_timeout_handler(struct k_work *work)
{
	LOG_WRN("Auto-connect timeout - falling back to active scan");
	LOG_DBG("Cancelling ongoing connection attempt");
	int err = bt_conn_create_auto_stop();
	if (err)
	{
		LOG_ERR("Failed to stop auto-connect (err %d)", err);
	}
}

static void auto_connect_work_handler(struct k_work *work)
{
	struct device_context *ctx = (work == &auto_connect_work[0].work) ? &device_ctx[0] : &device_ctx[1];

	LOG_INF("Connecting to device [DEVICE ID %d]", ctx->device_id);
	int err = bt_conn_le_create_auto(BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT);
	if (err)
	{
		LOG_ERR("Failed to set auto-connect (err %d)", err);
		// LOG_DBG("Starting active scan for devices");
		// ble_manager_start_scan_for_HIs();
		return;
	}

	// Set a timeout to fall back to active scanning if auto-connect doesn't work
	k_work_schedule(&auto_connect_timeout_work[ctx->device_id], K_MSEC(BT_AUTO_CONNECT_TIMEOUT_MS));
}

/**
 * @brief Schedule auto-connect for a device
 *
 * @param device_id Device ID (0 or 1)
 * @return 0 on success, negative error code on failure
 */
int schedule_auto_connect(uint8_t device_id)
{
	if (device_id > 1)
	{
		return -EINVAL;
	}

	LOG_DBG("Scheduling auto-connect [DEVICE ID %d]", device_id);
	k_work_schedule(&auto_connect_work[device_id], K_MSEC(0));
	return 0;
}

/** Initialize BLE manager
 * @brief Sets up connection callbacks, authentication, VCP controller, and battery reader
 *
 * @return 0 on success, negative error code on failure
 */
int ble_manager_init(void)
{
	bt_conn_auth_info_cb_register(&auth_info_callbacks);

	int err = ble_queues_init();
	if (err)
	{
		LOG_ERR("BLE queue init failed (err %d)", err);
		return err;
	}

	LOG_DBG("Initializing connection works");
	for (ssize_t i = 0; i < 2; i++)
	{
		k_work_init_delayable(&security_request_work[i], security_request_handler);
		k_work_init_delayable(&auto_connect_work[i], auto_connect_work_handler);
		k_work_init_delayable(&auto_connect_timeout_work[i], auto_connect_timeout_handler);
	}

	err = devices_manager_init();
	if (err)
	{
		LOG_ERR("Devices manager init failed (err %d)", err);
		return err;
	}

	bonded_devices = (struct bond_collection *)k_calloc(1, sizeof(struct bond_collection));
	if (!bonded_devices)
	{
		LOG_ERR("Failed to allocate memory for bonded devices");
		k_free(device_ctx);
		return -ENOMEM;
	}
	err = devices_manager_get_bonded_devices_collection(bonded_devices);
	if (err)
	{
		LOG_ERR("Failed to get bonded devices collection (err %d)", err);
		return err;
	}

	LOG_INF("BLE manager and subsystems initialized");
	return 0;
}

/**
 * @brief Auto-connect to a device by address.
 * @param addr Address of the device to connect to
 * @return 0 on success, negative error code on failure
 */
int ble_manager_autoconnect_to_device_by_addr(const bt_addr_le_t *addr)
{
	struct device_context *ctx = devices_manager_get_device_context_by_addr(addr);
	if (!ctx)
	{
		LOG_ERR("No device context found for address");
		return -EINVAL;
	}

	// Clear and add to filter accept list for auto-connect
	bt_le_filter_accept_list_clear();
	int err = bt_le_filter_accept_list_add(&ctx->info.addr);
	if (err && err != -EALREADY)
	{
		LOG_ERR("Failed to add device to filter accept list (err %d)", err);
	}

	// err = bt_le_set_rpa_timeout(900); // Set RPA timeout
	// if (err)
	// {
	// 	LOG_WRN("Failed to set RPA timeout (err %d)", err);
	// }

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&ctx->info.addr, addr_str, sizeof(addr_str));
	LOG_INF("Attempting to connect to device: %s", addr_str);
	if (ctx->state == CONN_STATE_DISCONNECTED)
		ctx->state = CONN_STATE_CONNECTING;

	k_work_schedule(&auto_connect_work[ctx->device_id], K_MSEC(0));
	return 0;
}

/**
 * @brief Auto-connect to a bonded device by address.
 * @param addr Address of the device to connect to
 * @return 0 on success, negative error code on failure
 */
int ble_manager_autoconnect_to_bonded_device(uint8_t device_id)
{
	struct device_context *ctx = &device_ctx[device_id];

	char addr_str[BT_ADDR_LE_STR_LEN];

	if (!ctx)
	{
		LOG_ERR("Invalid device context for auto-connect");
		return -EINVAL;
	}

	struct bonded_device_entry entry = bonded_devices->devices[device_id];
	if (bt_addr_le_cmp(&entry.addr, &bt_addr_le_none) == 0)
	{
		LOG_ERR("No bonded device found for auto-connect [DEVICE ID %d]", device_id);
		return -EINVAL;
	}

	bt_addr_le_to_str(&entry.addr, addr_str, sizeof(addr_str));
	LOG_DBG("Found entry in bonded devices for auto-connect, addr=%s [DEVICE ID %d]", addr_str, device_id);
	memset(ctx, 0, sizeof(struct device_context));
	bt_addr_le_copy(&ctx->info.addr, &entry.addr);
	ctx->device_id = device_id;
	ctx->state = CONN_STATE_BONDED;
	bt_addr_le_to_str(&ctx->info.addr, addr_str, sizeof(addr_str));
	LOG_DBG("Set device context for auto-connect, addr=%s [DEVICE ID %d]", addr_str, device_id);

	// Clear and add to filter accept list for auto-connect
	bt_le_filter_accept_list_clear();
	int err = bt_le_filter_accept_list_add(&ctx->info.addr);
	if (err && err != -EALREADY)
	{
		LOG_ERR("Failed to add device to filter accept list (err %d)", err);
	} else {
		bt_addr_le_to_str(&ctx->info.addr, addr_str, sizeof(addr_str));
		LOG_DBG("Added address to filter accept list for auto-connect (addr=%s) [DEVICE ID %d]", addr_str, device_id);
	}

	err = bt_le_set_rpa_timeout(900); // Set RPA timeout
	if (err)
	{
		LOG_WRN("Failed to set RPA timeout (err %d)", err);
	}

	LOG_INF("Attempting to connect to device: %s", addr_str);
	k_work_schedule(&auto_connect_work[ctx->device_id], K_MSEC(0));

	return 0;
}

void bt_ready_cb(int err)
{
	if (err)
	{
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	if (IS_ENABLED(CONFIG_SETTINGS))
	{
		LOG_DBG("Loading BT settings from flash");
		err = settings_load_subtree("bt");
		if (err)
		{
			LOG_WRN("Failed to load BT settings (err %d)", err);
		}
	}

	LOG_DBG("Clearing filter accept list");
	bt_le_filter_accept_list_clear();

	/* Initialize BLE manager */
	err = ble_manager_init();
	if (err)
	{
		LOG_ERR("BLE manager init failed (err %d)", err);
		return;
	}

	app_controller_notify_system_ready();
}

// static void is_bonded_device_cb(const struct bt_bond_info *info, void *user_data)
// {
// 	struct check_bonded_data
// 	{
// 		const bt_addr_le_t *target_addr;
// 		bool found;
// 	} *data = user_data;

// 	if (bt_addr_le_eq(&info->addr, data->target_addr))
// 	{
// 		data->found = true;
// 		LOG_DBG("Found bonded device");
// 	}
// }

// static bool is_bonded_device(const bt_addr_le_t *addr)
// {
// 	struct check_bonded_data
// 	{
// 		const bt_addr_le_t *target_addr;
// 		bool found;
// 	} check_data = {.target_addr = addr, .found = false};

// 	char addr_str[BT_ADDR_LE_STR_LEN];
// 	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
// 	LOG_DBG("Checking if device %s is bonded", addr_str);
// 	bt_foreach_bond(BT_ID_DEFAULT, is_bonded_device_cb, &check_data);

// 	return check_data.found;
// }

/* Execute a single BLE command */
static int ble_cmd_execute(struct ble_cmd *cmd)
{
	int err = 0;

	LOG_DBG("Executing BLE command type %s [DEVICE ID %d]", command_type_to_string(cmd->type), cmd->device_id);

	switch (cmd->type)
	{
	case BLE_CMD_REQUEST_SECURITY:
		k_work_schedule(&security_request_work[cmd->device_id], K_MSEC(0));
		break;

	/* VCP */
	case BLE_CMD_VCP_DISCOVER:
		err = vcp_cmd_discover(cmd->device_id);
		break;
	case BLE_CMD_VCP_VOLUME_UP:
		err = vcp_cmd_volume_up(cmd->device_id);
		break;
	case BLE_CMD_VCP_VOLUME_DOWN:
		err = vcp_cmd_volume_down(cmd->device_id);
		break;
	case BLE_CMD_VCP_SET_VOLUME:
		err = vcp_cmd_set_volume(cmd->device_id, cmd->d0);
		break;
	case BLE_CMD_VCP_MUTE:
		err = vcp_cmd_mute(cmd->device_id);
		break;
	case BLE_CMD_VCP_UNMUTE:
		err = vcp_cmd_unmute(cmd->device_id);
		break;
	case BLE_CMD_VCP_READ_STATE:
		err = vcp_cmd_read_state(cmd->device_id);
		break;
	case BLE_CMD_VCP_READ_FLAGS:
		err = vcp_cmd_read_flags(cmd->device_id);
		break;

	/* BAS */
	case BLE_CMD_BAS_DISCOVER:
		err = battery_discover(cmd->device_id);
		break;
	case BLE_CMD_BAS_READ_LEVEL:
		err = battery_read_level(cmd->device_id);
		break;

	/* CSIP */
	case BLE_CMD_CSIP_DISCOVER:
		err = csip_cmd_discover(cmd->device_id);
		break;
	
	/* HAS */
    case BLE_CMD_HAS_DISCOVER:
        err = has_cmd_discover();
        break;
    case BLE_CMD_HAS_READ_PRESETS:
        err = has_cmd_read_presets();
        break;
    case BLE_CMD_HAS_SET_PRESET:
        err = has_cmd_set_active_preset(cmd->d0);
        break;
    case BLE_CMD_HAS_NEXT_PRESET:
        err = has_cmd_next_preset();
        break;
    case BLE_CMD_HAS_PREV_PRESET:
        err = has_cmd_prev_preset();
        break;

	default:
		LOG_ERR("Unknown BLE command type: %d", cmd->type);
		err = -EINVAL;
		break;
	}

	if (err)
	{
		LOG_ERR("BLE command execution failed: type=%s, err=%d [DEVICE ID %d]", command_type_to_string(cmd->type), err, cmd->device_id);
	}
	else
	{
		LOG_DBG("BLE command initiated successfully: type=%s [DEVICE ID %d]", command_type_to_string(cmd->type), cmd->device_id);
	}

	return err;
}

/* Handle command timeout */
static void ble_cmd_timeout_handler(struct k_work *work)
{
	struct device_context *ctx = (work == &ble_cmd_timeout_work[0].work) ? &device_ctx[0] : &device_ctx[1];

	if (!ctx->current_ble_cmd)
	{
		LOG_WRN("Timeout but no current command");
		ble_cmd_in_progress[ctx->device_id] = false;
		return;
	}
	else
	{
		LOG_ERR("BLE command timeout (safety net): type=%d",
				ctx->current_ble_cmd->type);

		// Free the command and move on
		ble_cmd_free(ctx->current_ble_cmd);
		ctx->current_ble_cmd = NULL;
		ble_cmd_in_progress[ctx->device_id] = false;
	}

	// Process next command
	ble_process_next_command(ctx->device_id);
	return;
}

/* Mark command as complete (called when subsystem command completes) */
void ble_cmd_complete(uint8_t device_id, int err)
{
	struct device_context *ctx = &device_ctx[device_id];

	// Cancel timeout
	k_work_cancel_delayable(&ble_cmd_timeout_work[ctx->device_id]);

	if (!ctx->current_ble_cmd)
	{
		LOG_WRN("Command complete but no current command [DEVICE ID %d]", device_id);
		return;
	}

	if (err)
	{
		LOG_ERR("BLE command failed: type=%s, err=%d [DEVICE ID %d]", command_type_to_string(ctx->current_ble_cmd->type), err, device_id);

		if (ctx->current_ble_cmd->type >= 0x2 && ctx->current_ble_cmd->type <= 0x8)
		{
			if (err == 15)
			{
				// queue_is_active[ctx->device_id] = false;
				LOG_ERR("VCP command failed due to insufficient authentication - reconnecting [DEVICE ID %d]", ctx->device_id);
				ble_manager_disconnect_device(device_ctx[ctx->device_id].conn);
				switch (ctx->current_ble_cmd->type)
				{
				case BLE_CMD_VCP_VOLUME_UP:
					ble_cmd_vcp_volume_up(ctx->current_ble_cmd->device_id, true);
					break;
				case BLE_CMD_VCP_VOLUME_DOWN:
					ble_cmd_vcp_volume_down(ctx->current_ble_cmd->device_id, true);
					break;
				case BLE_CMD_VCP_SET_VOLUME:
					ble_cmd_vcp_set_volume(ctx->current_ble_cmd->device_id, ctx->current_ble_cmd->d0, true);
					break;
				case BLE_CMD_VCP_MUTE:
					ble_cmd_vcp_mute(ctx->current_ble_cmd->device_id, true);
					break;
				case BLE_CMD_VCP_UNMUTE:
					ble_cmd_vcp_unmute(ctx->current_ble_cmd->device_id, true);
					break;
				case BLE_CMD_VCP_READ_STATE:
					ble_cmd_vcp_read_state(ctx->current_ble_cmd->device_id, true);
					break;
				case BLE_CMD_VCP_READ_FLAGS:
					ble_cmd_vcp_read_flags(ctx->current_ble_cmd->device_id, true);
					break;
				default:
					break;
				}
			}
		}
	}
	else
	{
		LOG_DBG("BLE command completed successfully: type=%s [DEVICE ID %d]", command_type_to_string(ctx->current_ble_cmd->type), ctx->device_id);
	}

	// Free the command
	ble_cmd_free(ctx->current_ble_cmd);
	ctx->current_ble_cmd = NULL;
	ble_cmd_in_progress[ctx->device_id] = false;

	// Process next command
	if (!err)
		ble_process_next_command(ctx->device_id);
}

/* Process the next command in the queue */
static void ble_process_next_command(uint8_t device_id)
{
	struct device_context *ctx = &device_ctx[device_id];

	struct ble_cmd *cmd = ble_cmd_dequeue(ctx->device_id);
	if (!cmd)
	{
		LOG_DBG("No BLE commands in queue [DEVICE ID %d]", device_id);
		return;
	}

	ctx->current_ble_cmd = cmd;
	ble_cmd_in_progress[ctx->device_id] = true;

	// Execute the command
	int err = ble_cmd_execute(ctx->current_ble_cmd);

	if (err)
	{
		// Command failed to initiate
		LOG_ERR("Failed to initiate BLE command (err %d) [DEVICE ID %d]", err, device_id);

		if (err == -EBUSY)
		{
			LOG_WRN("Server was busy - skipping command: type=%s [DEVICE ID %d]", command_type_to_string(ctx->current_ble_cmd->type), device_id);
			ble_cmd_free(cmd);
			ctx->current_ble_cmd = NULL;
			ble_cmd_in_progress[ctx->device_id] = false;
		}

		ble_process_next_command(ctx->device_id);
		return;
	}

	// Wait for completion callback with timeout
	LOG_DBG("Command waiting for completion: type=%s [DEVICE ID %d]", command_type_to_string(cmd->type), device_id);
	k_work_schedule(&ble_cmd_timeout_work[cmd->device_id], K_MSEC(BLE_CMD_TIMEOUT_MS));
}

int ble_cmd_request_security(uint8_t device_id)
{
	struct ble_cmd *cmd = ble_cmd_alloc(device_id);
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->device_id = device_id;
	cmd->type = BLE_CMD_REQUEST_SECURITY;
	return ble_cmd_enqueue(cmd, true); // Security requests should always be high priority
}

int ble_cmd_vcp_discover(uint8_t device_id, bool high_priority)
{
	struct device_context *ctx = &device_ctx[device_id];

	struct ble_cmd *cmd = ble_cmd_alloc(device_id);
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->device_id = ctx->device_id;
	cmd->type = BLE_CMD_VCP_DISCOVER;
	return ble_cmd_enqueue(cmd, high_priority);
}

/**
 * @brief Enqueue a VCP volume up command
 * @param device_id Device ID
 * @param high_priority Not used for this command
 * @return 0 on success, negative error code on failure
 */
int ble_cmd_vcp_volume_up(uint8_t device_id, bool high_priority)
{
	(void) high_priority;
	struct device_context *ctx = &device_ctx[device_id];
	ble_cmd_vcp_read_state(ctx->device_id, false);

	struct ble_cmd *cmd = ble_cmd_alloc(ctx->device_id);
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->device_id = ctx->device_id;
	cmd->type = BLE_CMD_VCP_VOLUME_UP;
	return ble_cmd_enqueue(cmd, 0);
}

/**
 * @brief Enqueue a VCP volume down command
 * @param device_id Device ID
 * @param high_priority Not used for this command
 * @return 0 on success, negative error code on failure
 */
int ble_cmd_vcp_volume_down(uint8_t device_id, bool high_priority)
{
	(void) high_priority;
	struct device_context *ctx = &device_ctx[device_id];
	ble_cmd_vcp_read_state(ctx->device_id, false);

	struct ble_cmd *cmd = ble_cmd_alloc(ctx->device_id);
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->device_id = ctx->device_id;
	cmd->type = BLE_CMD_VCP_VOLUME_DOWN;
	return ble_cmd_enqueue(cmd, 0);
}

int ble_cmd_vcp_set_volume(uint8_t device_id, uint8_t volume, bool high_priority)
{
	struct device_context *ctx = &device_ctx[device_id];
	ble_cmd_vcp_read_state(ctx->device_id, false);

	struct ble_cmd *cmd = ble_cmd_alloc(ctx->device_id);
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->device_id = ctx->device_id;
	cmd->type = BLE_CMD_VCP_SET_VOLUME;
	cmd->d0 = volume;
	return ble_cmd_enqueue(cmd, high_priority);
}

int ble_cmd_vcp_mute(uint8_t device_id, bool high_priority)
{
	struct device_context *ctx = &device_ctx[device_id];
	ble_cmd_vcp_read_state(ctx->device_id, false);

	struct ble_cmd *cmd = ble_cmd_alloc(ctx->device_id);
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->device_id = ctx->device_id;
	cmd->type = BLE_CMD_VCP_MUTE;
	return ble_cmd_enqueue(cmd, high_priority);
}

int ble_cmd_vcp_unmute(uint8_t device_id, bool high_priority)
{
	struct device_context *ctx = &device_ctx[device_id];
	ble_cmd_vcp_read_state(ctx->device_id, false);

	struct ble_cmd *cmd = ble_cmd_alloc(ctx->device_id);
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->device_id = ctx->device_id;
	cmd->type = BLE_CMD_VCP_UNMUTE;
	return ble_cmd_enqueue(cmd, high_priority);
}

int ble_cmd_vcp_read_state(uint8_t device_id, bool high_priority)
{
	struct device_context *ctx = &device_ctx[device_id];
	struct ble_cmd *cmd = ble_cmd_alloc(ctx->device_id);
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->device_id = ctx->device_id;
	cmd->type = BLE_CMD_VCP_READ_STATE;
	return ble_cmd_enqueue(cmd, high_priority);
}

int ble_cmd_vcp_read_flags(uint8_t device_id, bool high_priority)
{
	struct device_context *ctx = &device_ctx[device_id];
	struct ble_cmd *cmd = ble_cmd_alloc(ctx->device_id);
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->device_id = ctx->device_id;
	cmd->type = BLE_CMD_VCP_READ_FLAGS;
	return ble_cmd_enqueue(cmd, high_priority);
}

int ble_cmd_bas_discover(uint8_t device_id, bool high_priority)
{
	struct device_context *ctx = &device_ctx[device_id];
	struct ble_cmd *cmd = ble_cmd_alloc(ctx->device_id);
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->device_id = ctx->device_id;
	cmd->type = BLE_CMD_BAS_DISCOVER;
	return ble_cmd_enqueue(cmd, high_priority);
}

int ble_cmd_bas_read_level(uint8_t device_id, bool high_priority)
{
	struct device_context *ctx = &device_ctx[device_id];
	struct ble_cmd *cmd = ble_cmd_alloc(ctx->device_id);
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->device_id = ctx->device_id;
	cmd->type = BLE_CMD_BAS_READ_LEVEL;
	return ble_cmd_enqueue(cmd, high_priority);
}

int ble_cmd_csip_discover(uint8_t device_id, bool high_priority)
{
	struct device_context *ctx = &device_ctx[device_id];
	struct ble_cmd *cmd = ble_cmd_alloc(ctx->device_id);
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->device_id = ctx->device_id;
	cmd->type = BLE_CMD_CSIP_DISCOVER;
	return ble_cmd_enqueue(cmd, high_priority);
}

/* Reset BLE command queue */
void ble_cmd_queue_reset(uint8_t device_id)
{
	struct device_context *ctx = &device_ctx[device_id];

	// Clear command queues
	k_mutex_lock(&ble_queue_mutex[ctx->device_id], K_FOREVER);

	struct ble_cmd *cmd;
	while ((cmd = (struct ble_cmd *)sys_slist_get(&ble_cmd_queue[ctx->device_id])) != NULL)
	{
		ble_cmd_free(cmd);
	}
	k_mutex_unlock(&ble_queue_mutex[ctx->device_id]);

	// Cancel any pending command
	if (ctx->current_ble_cmd)
	{
		ble_cmd_free(ctx->current_ble_cmd);
		ctx->current_ble_cmd = NULL;
	}

	ble_cmd_in_progress[ctx->device_id] = false;
	k_work_cancel_delayable(&ble_cmd_timeout_work[ctx->device_id]);

	LOG_DBG("BLE command queue reset");
}

/* Command processing thread */
static void ble_cmd_thread_0(void)
{
	LOG_INF("BLE command thread 0 started");

	while (1)
	{
		// Wait for a command to be enqueued
		k_sem_take(&ble_cmd_sem[0], K_FOREVER);

		// Process the next command only if nothing is in progress
		// If a command is already in progress, it will call ble_process_next_command()
		// when it completes via ble_cmd_complete()
		if (!device_ctx[0].current_ble_cmd /*&& queue_is_active[0]*/)
			ble_process_next_command(0);
	}
}

static void ble_cmd_thread_1(void)
{
	LOG_INF("BLE command thread 1 started");

	while (1)
	{
		// Wait for a command to be enqueued
		k_sem_take(&ble_cmd_sem[1], K_FOREVER);

		// Process the next command only if nothing is in progress
		// If a command is already in progress, it will call ble_process_next_command_1()
		// when it completes via ble_cmd_complete()
		if (!device_ctx[1].current_ble_cmd /*&& queue_is_active[1]*/)
			ble_process_next_command(1);
	}
}

/* Command thread */
K_THREAD_DEFINE(ble_cmd_thread_0_id, 1024, ble_cmd_thread_0, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(ble_cmd_thread_1_id, 1024, ble_cmd_thread_1, NULL, NULL, NULL, 7, 0, 0);

static char *command_type_to_string(enum ble_cmd_type type)
{
	switch (type)
	{
	case BLE_CMD_REQUEST_SECURITY:
		return "BLE_CMD_REQUEST_SECURITY";
	case BLE_CMD_VCP_DISCOVER:
		return "BLE_CMD_VCP_DISCOVER";
	case BLE_CMD_VCP_VOLUME_UP:
		return "BLE_CMD_VCP_VOLUME_UP";
	case BLE_CMD_VCP_VOLUME_DOWN:
		return "BLE_CMD_VCP_VOLUME_DOWN";
	case BLE_CMD_VCP_SET_VOLUME:
		return "BLE_CMD_VCP_SET_VOLUME";
	case BLE_CMD_VCP_MUTE:
		return "BLE_CMD_VCP_MUTE";
	case BLE_CMD_VCP_UNMUTE:
		return "BLE_CMD_VCP_UNMUTE";
	case BLE_CMD_VCP_READ_STATE:
		return "BLE_CMD_VCP_READ_STATE";
	case BLE_CMD_VCP_READ_FLAGS:
		return "BLE_CMD_VCP_READ_FLAGS";
	case BLE_CMD_BAS_DISCOVER:
		return "BLE_CMD_BAS_DISCOVER";
	case BLE_CMD_BAS_READ_LEVEL:
		return "BLE_CMD_BAS_READ_LEVEL";
	case BLE_CMD_CSIP_DISCOVER:
		return "BLE_CMD_CSIP_DISCOVER";
	case BLE_CMD_HAS_DISCOVER:
		return "BLE_CMD_HAS_DISCOVER";
	case BLE_CMD_HAS_READ_PRESETS:
		return "BLE_CMD_HAS_READ_PRESETS";
	case BLE_CMD_HAS_SET_PRESET:
		return "BLE_CMD_HAS_SET_PRESET";
	case BLE_CMD_HAS_NEXT_PRESET:
		return "BLE_CMD_HAS_NEXT_PRESET";
	case BLE_CMD_HAS_PREV_PRESET:
		return "BLE_CMD_HAS_PREV_PRESET";
	default:
		return "UNKNOWN_COMMAND";
	}
}

/* Called from the battery_reader notification cb*/
void ble_manager_set_device_ctx_battery_level(struct bt_conn *conn, uint8_t level)
{
	struct device_context *ctx = devices_manager_get_device_context_by_conn(conn);
	if (!ctx)
	{
		LOG_WRN("Battery level update from unknown connection");
		return;
	}

	ctx->bas_ctlr.battery_level = level;
}

	/* Public API - Hearing Access Service Commands */
int ble_cmd_has_discover(bool high_priority)
{
	struct ble_cmd *cmd = ble_cmd_alloc();
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->type = BLE_CMD_HAS_DISCOVER;
	return ble_cmd_enqueue(cmd, high_priority);
}

int ble_cmd_has_read_presets(bool high_priority)
{
	struct ble_cmd *cmd = ble_cmd_alloc();
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->type = BLE_CMD_HAS_READ_PRESETS;
	return ble_cmd_enqueue(cmd, high_priority);
}

int ble_cmd_has_set_preset(uint8_t preset_index, bool high_priority)
{
	struct ble_cmd *cmd = ble_cmd_alloc();
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->type = BLE_CMD_HAS_SET_PRESET;
	cmd->d0 = preset_index;
	return ble_cmd_enqueue(cmd, high_priority);
}

int ble_cmd_has_next_preset(bool high_priority)
{
	struct ble_cmd *cmd = ble_cmd_alloc();
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->type = BLE_CMD_HAS_NEXT_PRESET;
	return ble_cmd_enqueue(cmd, high_priority);
}

int ble_cmd_has_prev_preset(bool high_priority)
{
	struct ble_cmd *cmd = ble_cmd_alloc();
	if (!cmd)
	{
		return -ENOMEM;
	}

	cmd->type = BLE_CMD_HAS_PREV_PRESET;
	return ble_cmd_enqueue(cmd, high_priority);
}