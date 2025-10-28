#include "ble_manager.h"
#include "vcp_controller.h"
#include "battery_reader.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/hci.h>

LOG_MODULE_REGISTER(ble_manager, LOG_LEVEL_DBG);

struct connection_context *conn_ctx;
static struct k_work_delayable auto_connect_work;
static struct k_work_delayable auto_connect_timeout_work;
static struct k_work_delayable security_request_work;

/* BLE Command queue state */
static sys_slist_t ble_cmd_queue;
static struct k_mutex ble_queue_mutex;
static struct k_sem ble_cmd_sem;
static struct k_work_delayable ble_cmd_timeout_work;
static struct ble_cmd *current_ble_cmd = NULL;
static bool ble_cmd_in_progress = false;

/* Memory pool for BLE commands */
K_MEM_SLAB_DEFINE(ble_cmd_slab, sizeof(struct ble_cmd), BLE_CMD_QUEUE_SIZE, 4);

/* Forward declarations */
static void ble_process_next_command(void);
static void ble_cmd_timeout_handler(struct k_work *work);

/* Command queue initialization */
static int ble_queue_init(void)
{
    sys_slist_init(&ble_cmd_queue);
    k_mutex_init(&ble_queue_mutex);
    k_sem_init(&ble_cmd_sem, 0, 1);
    k_work_init_delayable(&ble_cmd_timeout_work, ble_cmd_timeout_handler);

    return 0;
}

/* Allocate a command from memory pool */
static struct ble_cmd *ble_cmd_alloc(void)
{
    struct ble_cmd *cmd;

    if (k_mem_slab_alloc(&ble_cmd_slab, (void **)&cmd, K_NO_WAIT) != 0) {
        LOG_ERR("Failed to allocate BLE command - queue full");
        return NULL;
    }

    memset(cmd, 0, sizeof(struct ble_cmd));
    return cmd;
}

/* Free a command back to memory pool */
static void ble_cmd_free(struct ble_cmd *cmd)
{
    if (cmd) {
        k_mem_slab_free(&ble_cmd_slab, (void *)cmd);
    }
}

/* Enqueue a command */
static int ble_cmd_enqueue(struct ble_cmd *cmd)
{
    if (!cmd) {
        return -EINVAL;
    }

    k_mutex_lock(&ble_queue_mutex, K_FOREVER);
    sys_slist_append(&ble_cmd_queue, &cmd->node);
    k_mutex_unlock(&ble_queue_mutex);

    // Signal the processing thread
    k_sem_give(&ble_cmd_sem);

    LOG_DBG("BLE command enqueued, type: %d", cmd->type);
    return 0;
}

/* Dequeue a command */
static struct ble_cmd *ble_cmd_dequeue(void)
{
    struct ble_cmd *cmd = NULL;

    k_mutex_lock(&ble_queue_mutex, K_FOREVER);
    sys_snode_t *node = sys_slist_get(&ble_cmd_queue);
    if (node) {
        cmd = CONTAINER_OF(node, struct ble_cmd, node);
    }
    k_mutex_unlock(&ble_queue_mutex);

    return cmd;
}

static void security_request_handler(struct k_work *work)
{
    if (!conn_ctx->conn) {
		LOG_WRN("No connection to secure");
        return;
    }
    
    LOG_DBG("Requesting security level %d", BT_SECURITY_WANTED);
    int err = bt_conn_set_security(conn_ctx->conn, BT_SECURITY_WANTED);
    if (err) {
        LOG_ERR("Failed to set security (err %d)", err);
    }
		LOG_DBG("Security request initiated");
}

/* Callback for iterating bonded devices to find the first one */
static void get_bonded_devices(const struct bt_bond_info *info, void *user_data)
{
	struct deviceInfo *device = (struct deviceInfo *)user_data;

	if (!device->connect) { // Only copy first bonded device
		bt_addr_le_copy(&device->addr, &info->addr);
		device->connect = true;
		device->is_new_device = false;

		// Add to filter accept list for auto-connect
		int err = bt_le_filter_accept_list_add(&device->addr);
		if (err && err != -EALREADY) {
			LOG_ERR("Failed to add device to filter accept list (err %d)", err);
		}

		err = bt_le_set_rpa_timeout(900); // Set RPA timeout
		if (err) {
			LOG_WRN("Failed to set RPA timeout (err %d)", err);
		}

		char addr_str[BT_ADDR_LE_STR_LEN];
		bt_addr_le_to_str(&info->addr, addr_str, sizeof(addr_str));
		LOG_INF("Found bonded device: %s", addr_str);
	}
}

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
			LOG_DBG("Saving bond information to flash");
			settings_save();
		}

		bt_foreach_bond(BT_ID_DEFAULT, get_bonded_devices, &conn_ctx->info);

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
	disconnect(conn, NULL);
}

struct bt_conn_auth_info_cb auth_info_callbacks = {
	.pairing_complete = pairing_complete, // This is only called if new bond created
	.pairing_failed = pairing_failed, // Same for this - if it fails during new bond
};

void security_changed_cb(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	conn_ctx->conn = conn;

	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn_ctx->conn), addr, sizeof(addr));

	if (!err) {
		LOG_DBG("Security changed: %s level %u", addr, level);

		if (level >= BT_SECURITY_L2) {
			LOG_DBG("Encryption established at level %u", level);

			// Only proceed with VCP if this is a reconnection (not new pairing)
			if (conn_ctx->state == CONN_STATE_BONDED) {
				LOG_DBG("Bonded device encrypted - starting service discovery");
				ble_cmd_vcp_discover();
				ble_cmd_bas_discover();
			} else {
				LOG_DBG("New device - waiting for pairing completion");
				conn_ctx->state = CONN_STATE_PAIRING;
			}
		}
	} else {
		LOG_ERR("Security failed: %s level %u err %d", addr, level, err);
	}

	ble_cmd_complete(err);
}

void disconnect(struct bt_conn *conn, void *data)
{
	conn_ctx->conn = conn;

	LOG_INF("Disconnecting connection");
	bt_conn_disconnect(conn_ctx->conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	bt_conn_unref(conn_ctx->conn);
	conn_ctx->conn = NULL;
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed (err 0x%02X)", err);
		conn_ctx->state = CONN_STATE_DISCONNECTED;

		// Cancel auto-connect if it was active
		bt_conn_create_auto_stop();
		k_work_cancel_delayable(&auto_connect_timeout_work);

		if (err == BT_HCI_ERR_UNKNOWN_CONN_ID) {
			// Connection failed, retry
			ble_manager_scan_start();
		}
		return;
	}

	// Cancel the timeout since we connected successfully
	k_work_cancel_delayable(&auto_connect_timeout_work);

	const bt_addr_le_t *addr = bt_conn_get_dst(conn);
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	// Store connection reference
	if (conn_ctx->conn) {
		bt_conn_unref(conn_ctx->conn);
	}
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

	ble_cmd_request_security();
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason 0x%02X)", reason);

	if (conn_ctx->conn) {
		bt_conn_unref(conn_ctx->conn);
		conn_ctx->conn = NULL;
	}

	ble_manager_cmd_queue_reset();
	vcp_controller_reset();
	battery_reader_reset();

	if (conn_ctx->state == CONN_STATE_BONDED) {
		conn_ctx->state = CONN_STATE_DISCONNECTED;
		k_work_schedule(&auto_connect_work, K_MSEC(0));
	} else {
		conn_ctx->state = CONN_STATE_DISCONNECTED;
		LOG_DBG("Restarting scan to find bondable devices");
		ble_manager_scan_start();
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.security_changed = security_changed_cb,
};

/* Device discovery function
   Extracts device name from advertisement data */
static bool device_found(struct bt_data *data, void *user_data)
{
	struct deviceInfo *info = (struct deviceInfo *)user_data;
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&info->addr, addr_str, sizeof(addr_str));

	LOG_DBG("Advertisement data type 0x%X len %u from %s", data->type, data->data_len,
		addr_str);

	switch (data->type) {
	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_NAME_SHORTENED:
		char name[BT_NAME_MAX_LEN];
		memset(name, 0, sizeof(name));
		strncpy(name, (char *)data->data, MIN(data->data_len, BT_NAME_MAX_LEN - 1));

		LOG_DBG("Found device name: %.*s", data->data_len, (char *)data->data);
		int cmp = strcmp(name, "HARC HI");
		LOG_DBG("strcmp result: %d", cmp);
		if (!cmp) {
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

static uint8_t connect(struct deviceInfo info)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&info.addr, addr_str, sizeof(addr_str));
	LOG_INF("Connecting to %s (with address %s)", info.name, addr_str);

	bt_le_scan_stop();
	conn_ctx->state = CONN_STATE_CONNECTING;

	// BT_CONN_LE_CREATE_CONN uses 100% duty cycle
	int err = bt_conn_le_create(&info.addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT,
				    &conn_ctx->conn);
	if (err) {
		LOG_ERR("Create conn to %s failed (err %d)", info.name, err);
		if (err == -12) // -ENOMEM
		{
			bt_conn_foreach(BT_CONN_TYPE_LE, disconnect, NULL);
		}

		return err; // Will restart scan
	}

	return 0;
}

static void device_found_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			    struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	struct deviceInfo info = {0};
	info.addr = *addr;
	info.connect = false;

	bt_data_parse(ad, device_found, &info);

	if (info.connect) {
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
	if (err) {
		LOG_ERR("Stopping existing scan failed (err %d)", err);
		return;
	}

	bt_conn_unref(conn_ctx->conn);
	conn_ctx->conn = NULL;

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE_CAP_RAP, device_found_cb);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning for HIs");
}

static void auto_connect_timeout_handler(struct k_work *work)
{
	LOG_WRN("Auto-connect timeout - falling back to active scan");
	LOG_DBG("Cancelling ongoing connection attempt");
	int err = bt_conn_create_auto_stop();
	if (err) {
		LOG_ERR("Failed to stop auto-connect (err %d)", err);
	}

	k_sleep(K_MSEC(100));

	LOG_DBG("Starting active scan for devices");
	ble_manager_scan_start();
}

static void auto_connect_work_handler(struct k_work *work)
{
	(void)work;

	if (!conn_ctx->info.connect) {
		LOG_WRN("No bonded device stored - scanning for devices");
		ble_manager_scan_start();
		return;
	}

	LOG_INF("Connecting to previously bonded device");
	int err = bt_conn_le_create_auto(BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT);
	if (err) {
		LOG_ERR("Failed to set auto-connect (err %d)", err);
		LOG_DBG("Starting active scan for devices");
		ble_manager_scan_start();
		return;
	}

	// Set a timeout to fall back to active scanning if auto-connect doesn't work
	k_work_schedule(&auto_connect_timeout_work, K_SECONDS(4));
}

/** Initialize BLE manager
 * @brief Sets up connection callbacks, authentication, VCP controller, and battery reader
 *
 * @return 0 on success, negative error code on failure
 */
int ble_manager_init(void)
{
	bt_conn_auth_info_cb_register(&auth_info_callbacks);

	int err = ble_queue_init();
	if (err) {
		LOG_ERR("BLE queue init failed (err %d)", err);
		return err;
	}

	conn_ctx = (struct connection_context *)k_calloc(1, sizeof(struct connection_context));
	if (!conn_ctx) {
		LOG_ERR("Failed to allocate memory for connection context");
		return -ENOMEM;
	}

	LOG_DBG("Initializing connect work");
	k_work_init_delayable(&security_request_work, security_request_handler);
	k_work_init_delayable(&auto_connect_work, auto_connect_work_handler);
	k_work_init_delayable(&auto_connect_timeout_work, auto_connect_timeout_handler);

	LOG_INF("BLE manager initialized");
	return 0;
}

void bt_ready_cb(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	bt_le_filter_accept_list_clear();

	/* Initialize BLE manager */
	err = ble_manager_init();
	if (err) {
		LOG_ERR("BLE manager init failed (err %d)", err);
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_load_subtree("bt");
		if (err) {
			LOG_WRN("Failed to load BT settings (err %d)", err);
		}
	}

	// Check for bonded devices
	memset(&conn_ctx->info, 0, sizeof(conn_ctx->info));
	bt_foreach_bond(BT_ID_DEFAULT, get_bonded_devices, &conn_ctx->info);

	if (conn_ctx->info.connect) {
		LOG_DBG("Scheduling auto-connect to bonded device");
		k_work_schedule(&auto_connect_work, K_MSEC(0));
	} else {
		LOG_INF("No previously bonded device found");
		ble_manager_scan_start();
	}
}

static void is_bonded_device_cb(const struct bt_bond_info *info, void *user_data)
{
	struct check_bonded_data {
		const bt_addr_le_t *target_addr;
		bool found;
	} *data = user_data;

	if (bt_addr_le_eq(&info->addr, data->target_addr)) {
		data->found = true;
		LOG_DBG("Found bonded device");
	}
}

bool is_bonded_device(const bt_addr_le_t *addr)
{
	struct check_bonded_data {
		const bt_addr_le_t *target_addr;
		bool found;
	} check_data = {.target_addr = addr, .found = false};

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	LOG_DBG("Checking if device %s is bonded", addr_str);
	bt_foreach_bond(BT_ID_DEFAULT, is_bonded_device_cb, &check_data);

	return check_data.found;
}

/* Execute a single BLE command - dispatches to appropriate subsystem */
static int ble_execute_command(struct ble_cmd *cmd)
{
    int err = 0;

    LOG_DBG("Executing BLE command type %d", cmd->type);

    switch (cmd->type) {
		case BLE_CMD_REQUEST_SECURITY:
				k_work_schedule(&security_request_work, K_MSEC(0));
				break;

    /* VCP commands - route to VCP controller (has its own queue & completion handling) */
    case BLE_CMD_VCP_DISCOVER:
        err = vcp_cmd_discover();
        break;
    case BLE_CMD_VCP_VOLUME_UP:
        err = vcp_cmd_volume_up();
        break;
    case BLE_CMD_VCP_VOLUME_DOWN:
        err = vcp_cmd_volume_down();
        break;
    case BLE_CMD_VCP_SET_VOLUME:
        err = vcp_cmd_set_volume(cmd->d0);
        break;
    case BLE_CMD_VCP_MUTE:
        err = vcp_cmd_mute();
        break;
    case BLE_CMD_VCP_UNMUTE:
        err = vcp_cmd_unmute();
        break;
    case BLE_CMD_VCP_READ_STATE:
        err = vcp_cmd_read_state();
        break;
    case BLE_CMD_VCP_READ_FLAGS:
        err = vcp_cmd_read_flags();
        break;

    /* Battery Service commands - execute directly via GATT */
    case BLE_CMD_BAS_DISCOVER:
        err = battery_discover(conn_ctx);
        break;
    case BLE_CMD_BAS_READ_LEVEL:
        err = battery_read_level(conn_ctx);
        break;

    default:
        LOG_ERR("Unknown BLE command type: %d", cmd->type);
        err = -EINVAL;
        break;
    }

    if (err) {
        LOG_ERR("BLE command execution failed: type=%d, err=%d", cmd->type, err);
    } else {
        LOG_DBG("BLE command initiated successfully: type=%d", cmd->type);
    }

    return err;
}

/* Handle command timeout */
static void ble_cmd_timeout_handler(struct k_work *work)
{
    if (!current_ble_cmd) {
        LOG_WRN("Timeout but no current command");
        ble_cmd_in_progress = false;
        ble_process_next_command();
        return;
    }

    LOG_ERR("BLE command timeout (safety net): type=%d",
            current_ble_cmd->type);

    // Free the command and move on
    ble_cmd_free(current_ble_cmd);
    current_ble_cmd = NULL;
    ble_cmd_in_progress = false;

    // Process next command
    ble_process_next_command();
}

/* Mark command as complete (called when subsystem command completes) */
void ble_cmd_complete(int err)
{
    // Cancel timeout
    k_work_cancel_delayable(&ble_cmd_timeout_work);

    if (!current_ble_cmd) {
        LOG_WRN("Command complete but no current command");
        return;
    }

    if (err) {
        LOG_ERR("BLE command failed: type=%d, err=%d (VCP already retried if appropriate)",
                current_ble_cmd->type, err);
    } else {
        LOG_DBG("BLE command completed successfully: type=%d", current_ble_cmd->type);
    }

    // Free the command
    ble_cmd_free(current_ble_cmd);
    current_ble_cmd = NULL;
    ble_cmd_in_progress = false;

    // Process next command
    ble_process_next_command();
}

/* Process the next command in the queue */
static void ble_process_next_command(void)
{
    struct ble_cmd *cmd = ble_cmd_dequeue();
    if (!cmd) {
        LOG_DBG("No BLE commands in queue");
        return;
    }

    current_ble_cmd = cmd;
    ble_cmd_in_progress = true;

    // Check if this is a BAS command (they complete immediately)
    bool is_bas_cmd = (cmd->type == BLE_CMD_BAS_DISCOVER ||
                       cmd->type == BLE_CMD_BAS_READ_LEVEL);

    // Execute the command
    int err = ble_execute_command(cmd);

    if (err) {
        // Command failed to initiate
        LOG_ERR("Failed to initiate BLE command: %d", err);

        if (err == -ENOTCONN || err == -EINVAL) {
            // Not ready, re-queue and try later
            k_mutex_lock(&ble_queue_mutex, K_FOREVER);
            sys_slist_prepend(&ble_cmd_queue, &cmd->node);
            k_mutex_unlock(&ble_queue_mutex);
            current_ble_cmd = NULL;
            ble_cmd_in_progress = false;

            // Retry after delay
            k_work_schedule(&ble_cmd_timeout_work, K_MSEC(1000));
        } else {
            // Other error, drop command
            ble_cmd_free(cmd);
            current_ble_cmd = NULL;
            ble_cmd_in_progress = false;

            // Try next command
            ble_process_next_command();
        }
        return;
    }

		// Wait for completion callback with timeout
		LOG_DBG("Command waiting for completion: type=%d", cmd->type);
		k_work_schedule(&ble_cmd_timeout_work, K_MSEC(BLE_CMD_TIMEOUT_MS));
}

int ble_cmd_request_security(void)
{
    struct ble_cmd *cmd = ble_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }

    cmd->type = BLE_CMD_REQUEST_SECURITY;
    return ble_cmd_enqueue(cmd);
}

/* Public API - VCP Commands */
int ble_cmd_vcp_discover(void)
{
    struct ble_cmd *cmd = ble_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }

    cmd->type = BLE_CMD_VCP_DISCOVER;
    return ble_cmd_enqueue(cmd);
}

int ble_cmd_vcp_volume_up(void)
{
    struct ble_cmd *cmd = ble_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }

    cmd->type = BLE_CMD_VCP_VOLUME_UP;
    return ble_cmd_enqueue(cmd);
}

int ble_cmd_vcp_volume_down(void)
{
    struct ble_cmd *cmd = ble_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }

    cmd->type = BLE_CMD_VCP_VOLUME_DOWN;
    return ble_cmd_enqueue(cmd);
}

int ble_cmd_vcp_set_volume(uint8_t volume)
{
    struct ble_cmd *cmd = ble_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }

    cmd->type = BLE_CMD_VCP_SET_VOLUME;
    cmd->d0 = volume;
    return ble_cmd_enqueue(cmd);
}

int ble_cmd_vcp_mute(void)
{
    struct ble_cmd *cmd = ble_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }

    cmd->type = BLE_CMD_VCP_MUTE;
    return ble_cmd_enqueue(cmd);
}

int ble_cmd_vcp_unmute(void)
{
    struct ble_cmd *cmd = ble_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }

    cmd->type = BLE_CMD_VCP_UNMUTE;
    return ble_cmd_enqueue(cmd);
}

int ble_cmd_vcp_read_state(void)
{
    struct ble_cmd *cmd = ble_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }

    cmd->type = BLE_CMD_VCP_READ_STATE;
    return ble_cmd_enqueue(cmd);
}

int ble_cmd_vcp_read_flags(void)
{
    struct ble_cmd *cmd = ble_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }

    cmd->type = BLE_CMD_VCP_READ_FLAGS;
    return ble_cmd_enqueue(cmd);
}

/* Public API - Battery Service Commands */
int ble_cmd_bas_discover(void)
{
    struct ble_cmd *cmd = ble_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }

    cmd->type = BLE_CMD_BAS_DISCOVER;
    return ble_cmd_enqueue(cmd);
}

int ble_cmd_bas_read_level(void)
{
    struct ble_cmd *cmd = ble_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }

    cmd->type = BLE_CMD_BAS_READ_LEVEL;
    return ble_cmd_enqueue(cmd);
}

/* Reset BLE command queue */
void ble_manager_cmd_queue_reset(void)
{
    // Clear command queue
    k_mutex_lock(&ble_queue_mutex, K_FOREVER);
    struct ble_cmd *cmd;
    while ((cmd = (struct ble_cmd *)sys_slist_get(&ble_cmd_queue)) != NULL) {
        ble_cmd_free(cmd);
    }
    k_mutex_unlock(&ble_queue_mutex);

    // Cancel any pending command
    if (current_ble_cmd) {
        ble_cmd_free(current_ble_cmd);
        current_ble_cmd = NULL;
    }

    ble_cmd_in_progress = false;
    k_work_cancel_delayable(&ble_cmd_timeout_work);

    LOG_DBG("BLE command queue reset");
}

/* Command processing thread */
static void ble_cmd_thread(void)
{
    LOG_INF("BLE command thread started");

    while (1) {
        // Wait for a command to be enqueued
        k_sem_take(&ble_cmd_sem, K_FOREVER);

        // Process the next command only if nothing is in progress
        // If a command is already in progress, it will call ble_process_next_command()
        // when it completes via ble_cmd_complete()
        if (!ble_cmd_in_progress) {
            ble_process_next_command();
        }
    }
}

/* Command thread */
K_THREAD_DEFINE(ble_cmd_thread_id, 1024, ble_cmd_thread, NULL, NULL, NULL, 7, 0, 0);
