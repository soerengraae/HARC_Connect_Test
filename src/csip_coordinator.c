#include "csip_coordinator.h"
#include "ble_manager.h"
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(csip_coordinator, LOG_LEVEL_DBG);

int csip_cmd_discover(uint8_t device_id)
{
    struct device_context *ctx = &device_ctx[device_id];
    return bt_csip_set_coordinator_discover(ctx->conn);
}

static void csip_discover_cb(struct bt_conn *conn, const struct bt_csip_set_coordinator_set_member *members, int err, size_t set_count)
{
    struct device_context *ctx = get_device_context_by_conn(conn);

    if (err) {
        LOG_ERR("CSIP Coordinator discovery failed (err %d)", err);
    } else {
        LOG_INF("CSIP Coordinator discovered successfully");
        csip_discovered = true;
    }

    ble_cmd_complete(ctx->device_id, err);
}

static struct bt_csip_set_coordinator_cb csip_callbacks = {
    .discover = csip_discover_cb,
};

int csip_coordinator_init(void) {
    int err;

    err = bt_csip_set_coordinator_register_cb(&csip_callbacks);
    if (err) {
        LOG_ERR("Failed to register CSIP callbacks (err %d)", err);
        return err;
    }

    LOG_INF("CSIP Coordinator initialized");

    return 0;
}

/* CSIP Settings Management */

/**
 * @brief Store SIRK and rank for a bonded device
 *
 * @param addr Bluetooth address of the device
 * @param sirk SIRK value (16 bytes)
 * @param rank Device rank in the set (1 = left, 2 = right)
 * @return 0 on success, negative error code on failure
 */
int csip_settings_store_sirk(const bt_addr_le_t *addr, const uint8_t *sirk, uint8_t rank)
{
	if (!addr || !sirk) {
		return -EINVAL;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	// Create settings key for SIRK: "harc/device/<addr>/sirk"
	char key_sirk[64];
	snprintk(key_sirk, sizeof(key_sirk), "harc/device/%s/sirk", addr_str);

	// Create settings key for rank: "harc/device/<addr>/rank"
	char key_rank[64];
	snprintk(key_rank, sizeof(key_rank), "harc/device/%s/rank", addr_str);

	int err;

	// Store SIRK
	err = settings_save_one(key_sirk, sirk, CSIP_SIRK_SIZE);
	if (err) {
		LOG_ERR("Failed to store SIRK for %s (err %d)", addr_str, err);
		return err;
	}

	// Store rank
	err = settings_save_one(key_rank, &rank, sizeof(rank));
	if (err) {
		LOG_ERR("Failed to store rank for %s (err %d)", addr_str, err);
		return err;
	}

	LOG_INF("Stored CSIP info for %s: rank=%d", addr_str, rank);
	return 0;
}

/* Context for settings load callback */
struct csip_load_context {
	uint8_t *sirk;
	uint8_t *rank;
	bool sirk_found;
	bool rank_found;
};

/* Settings load callback for CSIP data */
static int csip_settings_load_cb(const char *key, size_t len, settings_read_cb read_cb,
                                   void *cb_arg, void *param)
{
	struct csip_load_context *ctx = (struct csip_load_context *)param;
	const char *name;

	if (!key) {
		return 0;
	}

	// Extract the leaf name from the key (after last '/')
	name = strrchr(key, '/');
	if (name) {
		name++; // Skip the '/'
	} else {
		name = key;
	}

	if (strcmp(name, "sirk") == 0) {
		if (len == CSIP_SIRK_SIZE) {
			read_cb(cb_arg, ctx->sirk, CSIP_SIRK_SIZE);
			ctx->sirk_found = true;
		}
	} else if (strcmp(name, "rank") == 0) {
		if (len == sizeof(uint8_t)) {
			read_cb(cb_arg, ctx->rank, sizeof(uint8_t));
			ctx->rank_found = true;
		}
	}

	return 0;
}

/**
 * @brief Load SIRK and rank for a bonded device
 *
 * @param addr Bluetooth address of the device
 * @param sirk Buffer to store SIRK (must be 16 bytes)
 * @param rank Pointer to store rank value
 * @return 0 on success, negative error code on failure
 */
int csip_settings_load_sirk(const bt_addr_le_t *addr, uint8_t *sirk, uint8_t *rank)
{
	if (!addr || !sirk || !rank) {
		return -EINVAL;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	// Create settings key base for this device
	char key_base[64];
	snprintk(key_base, sizeof(key_base), "harc/device/%s", addr_str);

	// Set up context for callback
	struct csip_load_context ctx = {
		.sirk = sirk,
		.rank = rank,
		.sirk_found = false,
		.rank_found = false,
	};

	// Load settings for this device
	int err = settings_load_subtree_direct(key_base, csip_settings_load_cb, &ctx);
	if (err) {
		LOG_DBG("Failed to load settings for %s (err %d)", addr_str, err);
		return -ENOENT;
	}

	if (!ctx.sirk_found || !ctx.rank_found) {
		LOG_DBG("CSIP data not found for %s (SIRK: %s, rank: %s)",
		        addr_str,
		        ctx.sirk_found ? "yes" : "no",
		        ctx.rank_found ? "yes" : "no");
		return -ENOENT;
	}

	LOG_DBG("Loaded CSIP info for %s: rank=%d", addr_str, *rank);
	return 0;
}

/**
 * @brief Clear CSIP settings for a device
 *
 * @param addr Bluetooth address of the device
 * @return 0 on success, negative error code on failure
 */
int csip_settings_clear_device(const bt_addr_le_t *addr)
{
	if (!addr) {
		return -EINVAL;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	// Create settings keys
	char key_sirk[64];
	char key_rank[64];
	snprintk(key_sirk, sizeof(key_sirk), "harc/device/%s/sirk", addr_str);
	snprintk(key_rank, sizeof(key_rank), "harc/device/%s/rank", addr_str);

	// Delete settings
	int err1 = settings_delete(key_sirk);
	int err2 = settings_delete(key_rank);

	if (err1 || err2) {
		LOG_WRN("Failed to clear settings for %s (SIRK: %d, rank: %d)",
		        addr_str, err1, err2);
	} else {
		LOG_INF("Cleared CSIP settings for %s", addr_str);
	}

	return (err1 || err2) ? -EIO : 0;
}