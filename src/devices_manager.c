#include "ble_manager.h"
#include "devices_manager.h"
#include "app_controller.h"
#include "csip_coordinator.h"
#include "display_manager.h"
#include "has_settings.h"
#include "vcp_settings.h"
#include "bas_settings.h"

LOG_MODULE_REGISTER(devices_manager, LOG_LEVEL_DBG);

struct device_context *device_ctx;
struct bond_collection *bonded_devices;

int devices_manager_get_bonded_devices_collection(struct bond_collection *collection)
{
	memcpy(collection, bonded_devices, sizeof(struct bond_collection));

	if (!collection) {
		LOG_ERR("No bonded devices collection available");
		return -ENOENT;
	}

	return 0;
}

/**
 * @brief Check if an address is in the bonded devices collection
 * @param addr Pointer to the address
 * @param out_entry Pointer to store the found bonded device entry, or NULL if not found
 * @return bool
 * @note out_entry must be allocated by the caller; if the device is not found, it will be set to
 * NULL
 */
bool devices_manager_find_bonded_entry_by_addr(const bt_addr_le_t *addr, struct bonded_device_entry *out_entry)
{
  for (uint8_t i = 0; i < bonded_devices->count; i++) {
		if (bt_addr_le_cmp(addr, &bonded_devices->devices[i].addr) == 0) {
			memcpy(out_entry, &bonded_devices->devices[i], sizeof(*out_entry));
			return true;
		}
  }

	LOG_DBG("Address not found in bonded devices collection");
	return false;
}

/* Callback for comprehensive bond enumeration */
static void enumerate_bonds_cb(const struct bt_bond_info *info, void *user_data)
{
	struct bond_collection *collection = (struct bond_collection *)user_data;

	if (collection->count >= CONFIG_BT_MAX_PAIRED) {
		LOG_WRN("Bond collection full, skipping device");
		return;
	}

	struct bonded_device_entry *entry = &collection->devices[collection->count];

	// Copy address
	bt_addr_le_copy(&entry->addr, &info->addr);

	// Try to load CSIP information from settings
	entry->has_sirk = false;
	entry->is_set_member = false;
	entry->set_rank = 0;
	memset(entry->sirk, 0, CSIP_SIRK_SIZE);

	// Load SIRK and rank from settings if available
	uint8_t rank = 0;
	if (csip_settings_load_sirk(&entry->addr, entry->sirk, &rank) == 0) {
		entry->has_sirk = true;
		entry->is_set_member = true;
		entry->set_rank = rank;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&entry->addr, addr_str, sizeof(addr_str));
	LOG_DBG("Enumerated bonded device %d: %s (SIRK: %s, Rank: %d)", collection->count, addr_str,
		entry->has_sirk ? "yes" : "no", entry->set_rank);

	collection->count++;
}

/**
 * @brief Enumerate all bonded devices and collect their metadata
 *
 * @param collection Pointer to bond_collection structure to fill
 * @return 0 on success, negative error code on failure
 */
static int enumerate_bonded_devices(struct bond_collection *collection)
{
	if (!collection) {
		return -EINVAL;
	}

	memset(collection, 0, sizeof(struct bond_collection));

	// Iterate through all bonded devices
	bt_foreach_bond(BT_ID_DEFAULT, enumerate_bonds_cb, collection);

	LOG_INF("Enumerated %d bonded device%s", collection->count,
		collection->count == 1 ? "" : "s");

	return 0;
}

void devices_manager_clear_all_bonds(void)
{
	LOG_WRN("Clearing all bonds...");

	for (ssize_t i = 0; i < bonded_devices->count; i++) {
		// Erase handles from memory
		int err = has_settings_clear_handles(&bonded_devices->devices[i].addr);
		if (err != 0) {
			LOG_ERR("Failed to clear HAS handles for device %d (err %d)", i, err);
		} else {
			LOG_DBG("Cleared HAS handles for device %d", i);
		}

		err = vcp_settings_clear_handles(&bonded_devices->devices[i].addr);
		if (err != 0) {
			LOG_ERR("Failed to clear VCP handles for device %d (err %d)", i, err);
		} else {
			LOG_DBG("Cleared VCP handles for device %d", i);
		}

		err = csip_settings_clear_device(&bonded_devices->devices[i].addr);
		if (err != 0) {
			LOG_ERR("Failed to clear CSIP device settings for device %d (err %d)", i, err);
		} else {
			LOG_DBG("Cleared CSIP device settings for device %d", i);
		}

		err = bas_settings_clear_handles(&bonded_devices->devices[i].addr);
		if (err != 0) {
			LOG_ERR("Failed to clear BAS handles for device %d (err %d)", i, err);
		} else {
			LOG_DBG("Cleared BAS handles for device %d", i);
		}

		err = bt_unpair(BT_ID_DEFAULT, &bonded_devices->devices[i].addr);
		if (err != 0) {
			LOG_ERR("Failed to unpair device %d (err %d)", i, err);
		} else {
			LOG_DBG("Unpaired bonded device %d", i);
		}
	}

	settings_save();

	// Erase bonds from RAM
	memset(bonded_devices, 0, sizeof(struct bond_collection));

	LOG_INF("All bonds cleared");
	app_controller_notify_bonds_cleared();
}

char *device_state_to_str(enum connection_state state)
{
	switch (state) {
	case CONN_STATE_DISCONNECTED:
		return "DISCONNECTED";
	case CONN_STATE_DISCONNECTING:
		return "DISCONNECTING";
	case CONN_STATE_CONNECTED:
		return "CONNECTED";
	case CONN_STATE_PAIRING:
		return "PAIRING";
	case CONN_STATE_PAIRED:
		return "PAIRED";
	case CONN_STATE_BONDED:
		return "BONDED";
	case CONN_STATE_READY:
		return "READY";
	case CONN_STATE_TRUSTING:
		return "TRUSTING";
	default:
		return "UNKNOWN";
	}
}

void devices_manager_set_device_state(struct device_context *ctx, enum connection_state state)
{
	if (!ctx) {
		LOG_ERR("Cannot set device state - context is NULL");
		return;
	}

	LOG_DBG("Device ID %d state changed from %s to %s", ctx->device_id, device_state_to_str(ctx->state), device_state_to_str(state));
	ctx->state = state;

	/* Update display with new connection state */
	display_manager_update_connection_state(ctx->device_id, device_state_to_str(state));
}

void devices_manager_update_bonded_devices_collection(void)
{
	LOG_INF("Updating bonded devices collection...");
	memset(bonded_devices, 0, sizeof(struct bond_collection));
	enumerate_bonded_devices(bonded_devices);
	LOG_INF("Bonded devices collection updated. Total bonded devices: %d",
		bonded_devices->count);
}

void devices_manager_reset_device_contexts(void) {
	device_ctx[0].device_id = 0;
	device_ctx[0].info.is_new_device = true;
	device_ctx[1].device_id = 1;
	device_ctx[1].info.is_new_device = true;
}

int devices_manager_init(void)
{
	device_ctx = (struct device_context *)k_calloc(2, sizeof(struct device_context));
	if (!device_ctx) {
		LOG_ERR("Failed to allocate memory for connection contexts");
		return -ENOMEM;
	}
	
	devices_manager_reset_device_contexts();

	bonded_devices = (struct bond_collection *)k_calloc(1, sizeof(struct bond_collection));
	if (!bonded_devices) {
		LOG_ERR("Failed to allocate memory for bonded devices");
		k_free(device_ctx);
		return -ENOMEM;
	}

	devices_manager_update_bonded_devices_collection();

	LOG_INF("Devices manager initialized");
	return 0;
}

struct device_context *devices_manager_get_device_context_by_conn(struct bt_conn *conn)
{
  LOG_DBG("Getting device context by connection");
	if (device_ctx[0].conn && bt_addr_le_cmp(bt_conn_get_dst(device_ctx[0].conn), bt_conn_get_dst(conn)) == 0) {
		LOG_DBG("Found matching device context for device ID 0");
		return &device_ctx[0];
	} else if (device_ctx[1].conn && bt_addr_le_cmp(bt_conn_get_dst(device_ctx[1].conn), bt_conn_get_dst(conn)) ==
		   0) {
		LOG_DBG("Found matching device context for device ID 1");
		return &device_ctx[1];
	} else {
		LOG_DBG("No device with matching connection found");
		return NULL;
	}
}

struct device_context *devices_manager_get_device_context_by_id(uint8_t device_id)
{
	if (device_id != 1 && device_id != 0) {
		LOG_ERR("Invalid device ID: %d", device_id);
		return NULL;
	}
	return &device_ctx[device_id];
}

/* Scanned devices list management */
static sys_slist_t scanned_devices_list;
static struct k_mutex scanned_list_mutex;
static uint8_t scanned_device_count = 0;
static bool scanned_list_initialized = false;

static void init_scanned_list(void)
{
	if (!scanned_list_initialized) {
		sys_slist_init(&scanned_devices_list);
		k_mutex_init(&scanned_list_mutex);
		scanned_list_initialized = true;
	}
}

int devices_manager_add_scanned_device(const bt_addr_le_t *addr, int8_t rssi)
{
	if (!addr) {
		return -EINVAL;
	}

	init_scanned_list();

	k_mutex_lock(&scanned_list_mutex, K_FOREVER);

	// Check if address already exists
	sys_snode_t *node;
	sys_snode_t *prev_node = NULL;
	SYS_SLIST_FOR_EACH_NODE(&scanned_devices_list, node) {
		struct scanned_device_entry *entry = CONTAINER_OF(node, struct scanned_device_entry, node);
		if (bt_addr_le_cmp(&entry->addr, addr) == 0) {
			// Address already in list, update RSSI and re-sort if needed
			if (entry->rssi != rssi) {
				entry->rssi = rssi;

				// Remove from current position
				if (prev_node == NULL) {
					sys_slist_get(&scanned_devices_list);
				} else {
					sys_slist_remove(&scanned_devices_list, prev_node, node);
				}

				// Re-insert in sorted order
				sys_snode_t *insert_prev = NULL;
				sys_snode_t *insert_curr;
				bool inserted = false;

				SYS_SLIST_FOR_EACH_NODE(&scanned_devices_list, insert_curr) {
					struct scanned_device_entry *curr_entry = CONTAINER_OF(insert_curr, struct scanned_device_entry, node);
					if (rssi > curr_entry->rssi) {
						if (insert_prev == NULL) {
							sys_slist_prepend(&scanned_devices_list, &entry->node);
						} else {
							sys_slist_insert(&scanned_devices_list, insert_prev, &entry->node);
						}
						inserted = true;
						break;
					}
					insert_prev = insert_curr;
				}

				if (!inserted) {
					sys_slist_append(&scanned_devices_list, &entry->node);
				}
			}
			k_mutex_unlock(&scanned_list_mutex);
			return scanned_device_count;
		}
		prev_node = node;
	}

	// Check if we've reached max devices
	if (scanned_device_count >= MAX_SCANNED_DEVICES) {
		k_mutex_unlock(&scanned_list_mutex);
		LOG_WRN("Scanned devices list full (max %d)", MAX_SCANNED_DEVICES);
		return scanned_device_count;
	}

	// Add new device
	struct scanned_device_entry *new_entry = k_malloc(sizeof(struct scanned_device_entry));
	if (!new_entry) {
		k_mutex_unlock(&scanned_list_mutex);
		LOG_ERR("Failed to allocate memory for scanned device");
		return -ENOMEM;
	}

	memset(new_entry, 0, sizeof(struct scanned_device_entry));
	bt_addr_le_copy(&new_entry->addr, addr);
	new_entry->rssi = rssi;

	// Insert device in sorted order by RSSI (highest first)
	prev_node = NULL;
	sys_snode_t *curr_node;
	bool inserted = false;

	SYS_SLIST_FOR_EACH_NODE(&scanned_devices_list, curr_node) {
		struct scanned_device_entry *curr_entry = CONTAINER_OF(curr_node, struct scanned_device_entry, node);
		if (rssi > curr_entry->rssi) {
			// Insert before this node
			if (prev_node == NULL) {
				// Insert at head
				sys_slist_prepend(&scanned_devices_list, &new_entry->node);
			} else {
				// Insert after prev_node
				sys_slist_insert(&scanned_devices_list, prev_node, &new_entry->node);
			}
			inserted = true;
			break;
		}
		prev_node = curr_node;
	}

	// If not inserted yet, append to the end (weakest RSSI)
	if (!inserted) {
		sys_slist_append(&scanned_devices_list, &new_entry->node);
	}

	scanned_device_count++;

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	LOG_INF("Added scanned device %d: %s (RSSI: %d)", scanned_device_count, addr_str, rssi);

	uint8_t count = scanned_device_count;
	k_mutex_unlock(&scanned_list_mutex);

	// If we've reached max devices, notify app_controller
	if (count >= MAX_SCANNED_DEVICES) {
		LOG_INF("Max scanned devices reached, stopping scan");
		ble_manager_stop_scan_for_HIs();
		app_controller_notify_scan_complete();
	}

	return count;
}

int devices_manager_update_scanned_device_name(const bt_addr_le_t *addr, const char *name)
{
	if (!addr || !name) {
		return -EINVAL;
	}

	init_scanned_list();

	k_mutex_lock(&scanned_list_mutex, K_FOREVER);

	// Find device by address
	sys_snode_t *node;
	SYS_SLIST_FOR_EACH_NODE(&scanned_devices_list, node) {
		struct scanned_device_entry *entry = CONTAINER_OF(node, struct scanned_device_entry, node);

		if (bt_addr_le_cmp(&entry->addr, addr) == 0) {
			strncpy(entry->name, name, BT_NAME_MAX_LEN - 1);
			entry->name[BT_NAME_MAX_LEN - 1] = '\0';

			char addr_str[BT_ADDR_LE_STR_LEN];
			bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
			LOG_DBG("Updated name for %s: %s", addr_str, name);

			k_mutex_unlock(&scanned_list_mutex);
			return 0;
		}
	}

	k_mutex_unlock(&scanned_list_mutex);
	return -ENOENT;
}

uint8_t devices_manager_get_scanned_device_count(void)
{
	init_scanned_list();

	k_mutex_lock(&scanned_list_mutex, K_FOREVER);
	uint8_t count = scanned_device_count;
	k_mutex_unlock(&scanned_list_mutex);

	return count;
}

struct scanned_device_entry *devices_manager_get_scanned_device(uint8_t idx)
{
	if (idx >= MAX_SCANNED_DEVICES) {
		return NULL;
	}

	init_scanned_list();

	k_mutex_lock(&scanned_list_mutex, K_FOREVER);

	if (idx >= scanned_device_count) {
		k_mutex_unlock(&scanned_list_mutex);
		return NULL;
	}

	sys_snode_t *node;
	uint8_t i = 0;
	SYS_SLIST_FOR_EACH_NODE(&scanned_devices_list, node) {
		if (i == idx) {
			struct scanned_device_entry *entry = CONTAINER_OF(node, struct scanned_device_entry, node);
			k_mutex_unlock(&scanned_list_mutex);
			return entry;
		}
		i++;
	}

	k_mutex_unlock(&scanned_list_mutex);
	return NULL;
}

void devices_manager_clear_scanned_devices(void)
{
	init_scanned_list();

	k_mutex_lock(&scanned_list_mutex, K_FOREVER);

	sys_snode_t *node;
	while ((node = sys_slist_get(&scanned_devices_list)) != NULL) {
		struct scanned_device_entry *entry = CONTAINER_OF(node, struct scanned_device_entry, node);
		k_free(entry);
	}

	scanned_device_count = 0;
	k_mutex_unlock(&scanned_list_mutex);

	LOG_INF("Scanned devices list cleared");
}

int devices_manager_select_scanned_device(uint8_t idx, struct device_info *out_info)
{
	if (!out_info) {
		return -EINVAL;
	}

	struct scanned_device_entry *entry = devices_manager_get_scanned_device(idx);
	if (!entry) {
		LOG_ERR("Invalid device index: %d", idx);
		return -ENOENT;
	}

	// Fill out_info with selected device
	memset(out_info, 0, sizeof(struct device_info));
	bt_addr_le_copy(&out_info->addr, &entry->addr);

	// Check if device was previously bonded
	struct bonded_device_entry bonded_entry;
	bool is_bonded = devices_manager_find_bonded_entry_by_addr(&entry->addr, &bonded_entry);
	out_info->is_new_device = !is_bonded;

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&out_info->addr, addr_str, sizeof(addr_str));
	LOG_INF("Selected scanned device %d: %s (%s) - %s", idx, addr_str, entry->name, out_info->is_new_device ? "new" : "bonded");

	return 0;
}

void devices_manager_print_scanned_devices() {
	init_scanned_list();

	k_mutex_lock(&scanned_list_mutex, K_FOREVER);

	LOG_INF("Scanned Devices List (Total: %d):", scanned_device_count);
	sys_snode_t *node;
	uint8_t idx = 0;
	SYS_SLIST_FOR_EACH_NODE(&scanned_devices_list, node) {
		struct scanned_device_entry *entry = CONTAINER_OF(node, struct scanned_device_entry, node);
		char addr_str[BT_ADDR_LE_STR_LEN];
		bt_addr_le_to_str(&entry->addr, addr_str, sizeof(addr_str));
		LOG_INF("  [%d] Name: %s | RSSI: %d | Address: %s", idx,
			strlen(entry->name) > 0 ? entry->name : "<unknown>", entry->rssi, addr_str);

		idx++;
	}

	k_mutex_unlock(&scanned_list_mutex);
}

struct device_context *devices_manager_get_device_context_by_addr(const bt_addr_le_t *addr)
{
  LOG_DBG("Getting device context by address");
	if (bt_addr_le_cmp(&device_ctx[0].info.addr, addr) == 0) {
		return &device_ctx[0];
	} else if (bt_addr_le_cmp(&device_ctx[1].info.addr, addr) == 0) {
		return &device_ctx[1];
	} else {
		LOG_DBG("No device with matching address found");
		return NULL;
	}
}