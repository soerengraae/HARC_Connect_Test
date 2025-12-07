/**
 * @file vcp_settings.c
 * @brief VCP handle caching in NVS settings
 */

#include "vcp_settings.h"

#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(vcp_settings, LOG_LEVEL_DBG);

/**
 * @brief Store VCP handles to NVS
 */
int vcp_settings_store_handles(const bt_addr_le_t *addr,
                                const struct bt_vcp_vol_ctlr_handles *handles)
{
	if (!addr || !handles) {
		return -EINVAL;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	/* Create settings key: "harc/device/<addr>/vcp_handles" */
	char key[64];
	snprintk(key, sizeof(key), "harc/device/%s/vcp_handles", addr_str);

	/* Store handles as binary blob */
	int err = settings_save_one(key, handles, sizeof(*handles));
	if (err) {
		LOG_ERR("Failed to store VCP handles for %s (err %d)", addr_str, err);
		return err;
	}

	LOG_INF("Stored VCP handles for %s at %s", addr_str, key);
	LOG_INF("  state: %u (ccc: %u)", handles->state_handle, handles->state_ccc_handle);
	LOG_INF("  control: %u", handles->control_handle);
	LOG_INF("  vol_flag: %u (ccc: %u)", handles->vol_flag_handle, handles->vol_flag_ccc_handle);
	return 0;
}

/* Context for settings load callback */
struct vcp_load_context {
	struct bt_vcp_vol_ctlr_handles *handles;
	bool found;
};

/* Settings load callback for VCP handles */
static int vcp_settings_load_cb(const char *key, size_t len, settings_read_cb read_cb,
                                 void *cb_arg, void *param)
{
	struct vcp_load_context *ctx = (struct vcp_load_context *)param;
	const char *name;

	if (!key) {
		return 0;
	}

	/* Extract the leaf name from the key (after last '/') */
	name = strrchr(key, '/');
	if (name) {
		name++; /* Skip the '/' */
	} else {
		name = key;
	}

	if (strcmp(name, "vcp_handles") == 0) {
		if (len == sizeof(struct bt_vcp_vol_ctlr_handles)) {
			read_cb(cb_arg, ctx->handles, sizeof(struct bt_vcp_vol_ctlr_handles));
			ctx->found = true;
		} else {
			LOG_WRN("Invalid VCP handles size: %zu (expected %zu)",
			        len, sizeof(struct bt_vcp_vol_ctlr_handles));
		}
	}

	return 0;
}

/**
 * @brief Load VCP handles from NVS
 */
int vcp_settings_load_handles(const bt_addr_le_t *addr,
                               struct bt_vcp_vol_ctlr_handles *handles)
{
	if (!addr || !handles) {
		return -EINVAL;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	/* Create settings key base for this device */
	char key_base[64];
	snprintk(key_base, sizeof(key_base), "harc/device/%s", addr_str);
	LOG_DBG("Loading VCP handles for %s at %s", addr_str, key_base);

	/* Set up context for callback */
	struct vcp_load_context ctx = {
		.handles = handles,
		.found = false,
	};

	/* Load settings for this device */
	int err = settings_load_subtree_direct(key_base, vcp_settings_load_cb, &ctx);
	if (err) {
		LOG_DBG("Failed to load settings for %s (err %d)", addr_str, err);
		return -ENOENT;
	}

	if (!ctx.found) {
		LOG_DBG("VCP handles not found for %s", addr_str);
		return -ENOENT;
	}

	LOG_INF("Loaded VCP handles for %s", addr_str);
	LOG_INF("  state: %u (ccc: %u)", handles->state_handle, handles->state_ccc_handle);
	LOG_INF("  control: %u", handles->control_handle);
	LOG_INF("  vol_flag: %u (ccc: %u)", handles->vol_flag_handle, handles->vol_flag_ccc_handle);
	return 0;
}

/**
 * @brief Clear VCP handles from NVS
 */
int vcp_settings_clear_handles(const bt_addr_le_t *addr)
{
	if (!addr) {
		return -EINVAL;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	/* Create settings key: "harc/device/<addr>/vcp_handles" */
	char key[64];
	snprintk(key, sizeof(key), "harc/device/%s/vcp_handles", addr_str);

	/* Delete the setting */
	int err = settings_delete(key);
	if (err) {
		LOG_ERR("Failed to clear VCP handles for %s (err %d)", addr_str, err);
		return err;
	}

	LOG_INF("Cleared VCP handles for %s", addr_str);
	return 0;
}
