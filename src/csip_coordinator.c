#include "csip_coordinator.h"
#include "ble_manager.h"
#include "devices_manager.h"


LOG_MODULE_REGISTER(csip_coordinator, LOG_LEVEL_DBG);

/* CSIP coordinator context per device */
struct csip_coordinator_context {
	uint8_t sirk[CSIP_SIRK_SIZE];
	bool sirk_discovered;
	uint8_t rank;
	uint8_t set_size;
	bool lock_supported;
};

static struct csip_coordinator_context csip_ctx[2];  // One per device

int csip_cmd_discover(uint8_t device_id)
{
    struct device_context *ctx = &device_ctx[device_id];
    return bt_csip_set_coordinator_discover(ctx->conn);
}

static void csip_discover_cb(struct bt_conn *conn, const struct bt_csip_set_coordinator_set_member *members, int err, size_t set_count)
{
    struct device_context *dev_ctx = get_device_context_by_conn(conn);
    struct csip_coordinator_context *ctx = &csip_ctx[dev_ctx->device_id];

    if (err) {
        LOG_ERR("CSIP Coordinator discovery failed (err %d) [DEVICE ID %d]", err, dev_ctx->device_id);
        ble_cmd_complete(dev_ctx->device_id, err);
        return;
    }

    LOG_INF("CSIP Coordinator discovered successfully [DEVICE ID %d]", dev_ctx->device_id);
    LOG_INF("  Set count: %zu", set_count);
    dev_ctx->info.csip_discovered = true;

    if (set_count == 0 || !members) {
        LOG_WRN("No set members discovered [DEVICE ID %d]", dev_ctx->device_id);
        ble_cmd_complete(dev_ctx->device_id, -ENODATA);
        return;
    }

    // Extract SIRK and rank from the first set member
    const struct bt_csip_set_coordinator_set_member *member = &members[0];

    // Get SIRK
    const uint8_t *sirk_data = member->insts[0].info.sirk;
    if (sirk_data) {
        memcpy(ctx->sirk, sirk_data, CSIP_SIRK_SIZE);
        ctx->sirk_discovered = true;

        LOG_INF("  SIRK extracted successfully [DEVICE ID %d]", dev_ctx->device_id);
        LOG_HEXDUMP_DBG(ctx->sirk, CSIP_SIRK_SIZE, "SIRK:");
    } else {
        LOG_ERR("Failed to extract SIRK [DEVICE ID %d]", dev_ctx->device_id);
        ctx->sirk_discovered = false;
    }

    // Get rank
    uint8_t rank = member->insts[0].info.rank;
    ctx->rank = rank;
    LOG_INF("  Rank: %d [DEVICE ID %d]", rank, dev_ctx->device_id);

    // Get set size
    ctx->set_size = member->insts[0].info.set_size;
    LOG_INF("  Set size: %d [DEVICE ID %d]", ctx->set_size, dev_ctx->device_id);

    // Check if lock is supported
    ctx->lock_supported = member->insts[0].info.lockable;
    LOG_INF("  Lock supported: %s [DEVICE ID %d]",
            ctx->lock_supported ? "yes" : "no", dev_ctx->device_id);

    // Store SIRK and rank to settings for future reconnections
    if (ctx->sirk_discovered) {
        const bt_addr_le_t *addr = bt_conn_get_dst(conn);
        int store_err = csip_settings_store_sirk(addr, ctx->sirk, ctx->rank);
        if (store_err) {
            LOG_ERR("Failed to store CSIP data to settings (err %d) [DEVICE ID %d]",
                    store_err, dev_ctx->device_id);
        } else {
            LOG_INF("CSIP data stored to flash [DEVICE ID %d]", dev_ctx->device_id);
        }
    }

    // Notify state machine that CSIP discovery is complete
    // The state machine will handle RSI scanning if needed
    // connection_state_machine_on_csip_discovered(dev_ctx->device_id);

    ble_cmd_complete(dev_ctx->device_id, 0);
}

static void csip_sirk_changed_cb(struct bt_csip_set_coordinator_csis_inst *inst)
{
	LOG_WRN("CSIP SIRK changed on remote device.");
}

static struct bt_csip_set_coordinator_cb csip_callbacks = {
    .discover = csip_discover_cb,
	.sirk_changed = csip_sirk_changed_cb,
};

int csip_coordinator_init(void) {
    int err;

    // Initialize contexts
    memset(csip_ctx, 0, sizeof(csip_ctx));

    err = bt_csip_set_coordinator_register_cb(&csip_callbacks);
    if (err) {
        LOG_ERR("Failed to register CSIP callbacks (err %d)", err);
        return err;
    }

    LOG_INF("CSIP Coordinator initialized");

    return 0;
}

/**
 * @brief Get SIRK and rank for a device
 *
 * @param device_id Device ID (0 or 1)
 * @param sirk_out Buffer to store SIRK (16 bytes), can be NULL
 * @param rank_out Pointer to store rank, can be NULL
 * @return true if SIRK was discovered, false otherwise
 */
bool csip_get_sirk(uint8_t device_id, uint8_t *sirk_out, uint8_t *rank_out)
{
    if (device_id > 1) {
        return false;
    }

    struct csip_coordinator_context *ctx = &csip_ctx[device_id];

    if (!ctx->sirk_discovered) {
        return false;
    }

    if (sirk_out) {
        memcpy(sirk_out, ctx->sirk, CSIP_SIRK_SIZE);
    }

    if (rank_out) {
        *rank_out = ctx->rank;
    }

    return true;
}

/**
 * @brief Verify that two devices are members of the same set
 *
 * @param device_id_1 First device ID
 * @param device_id_2 Second device ID
 * @return true if both devices have matching SIRKs, false otherwise
 */
bool csip_verify_set_membership(uint8_t device_id_1, uint8_t device_id_2)
{
    if (device_id_1 > 1 || device_id_2 > 1) {
        return false;
    }

    struct csip_coordinator_context *ctx1 = &csip_ctx[device_id_1];
    struct csip_coordinator_context *ctx2 = &csip_ctx[device_id_2];

    if (!ctx1->sirk_discovered || !ctx2->sirk_discovered) {
        LOG_WRN("Cannot verify set membership - SIRK not discovered for both devices");
        return false;
    }

    bool match = (memcmp(ctx1->sirk, ctx2->sirk, CSIP_SIRK_SIZE) == 0);

    if (match) {
        LOG_INF("Set membership verified - devices %d and %d are in the same set",
                device_id_1, device_id_2);
        LOG_INF("  Device %d rank: %d", device_id_1, ctx1->rank);
        LOG_INF("  Device %d rank: %d", device_id_2, ctx2->rank);
    } else {
        LOG_WRN("Set membership FAILED - devices %d and %d have different SIRKs",
                device_id_1, device_id_2);
    }

    return match;
}

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