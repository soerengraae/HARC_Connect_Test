#include "ble_manager.h"
#include "devices_manager.h"
#include "app_controller.h"
#include "csip_coordinator.h"

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
 * @note out_entry can be allocated by the caller; if the device is not found, it will be set to
 * NULL
 */
bool devices_manager_find_entry_by_addr(const bt_addr_le_t *addr, struct bonded_device_entry *out_entry)
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
	memset(entry->name, 0, BT_NAME_MAX_LEN);

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

	// Clear connections if any exist
	if (device_ctx[0].conn) {
		LOG_INF("Disconnecting before clearing bonds...");
		disconnect(device_ctx[0].conn, NULL);
	}

	if (device_ctx[1].conn) {
		LOG_INF("Disconnecting before clearing bonds...");
		disconnect(device_ctx[1].conn, NULL);
	}

	memset(bonded_devices, 0, sizeof(struct bond_collection));

	LOG_INF("All bonds cleared");
}

void devices_manager_update_bonded_devices_collection(void)
{
	LOG_INF("Updating bonded devices collection...");
	memset(bonded_devices, 0, sizeof(struct bond_collection));
	enumerate_bonded_devices(bonded_devices);
	LOG_INF("Bonded devices collection updated. Total bonded devices: %d",
		bonded_devices->count);
}

int devices_manager_init(void)
{
	device_ctx = (struct device_context *)k_calloc(2, sizeof(struct device_context));
	if (!device_ctx) {
		LOG_ERR("Failed to allocate memory for connection contexts");
		return -ENOMEM;
	}
	device_ctx[0].device_id = 0;
	device_ctx[0].info.is_new_device = true;
	device_ctx[1].device_id = 1;
	device_ctx[1].info.is_new_device = true;

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

struct device_context *get_device_context_by_conn(struct bt_conn *conn)
{
  LOG_DBG("Getting device context by connection");
	if (bt_addr_le_cmp(bt_conn_get_dst(device_ctx[0].conn), bt_conn_get_dst(conn)) == 0) {
		return &device_ctx[0];
	} else if (bt_addr_le_cmp(bt_conn_get_dst(device_ctx[1].conn), bt_conn_get_dst(conn)) ==
		   0) {
		return &device_ctx[1];
	} else {
		LOG_DBG("No device with matching connection found");
		return NULL;
	}
}

struct device_context *get_device_context_by_id(uint8_t device_id)
{
	if (device_id != 1 && device_id != 0) {
		LOG_ERR("Invalid device ID: %d", device_id);
		return NULL;
	}
	return &device_ctx[device_id];
}
