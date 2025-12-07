/**
 * @file has_settings.c
 * @brief HAS handle caching in NVS settings
 */

#include "has_settings.h"

#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(has_settings, LOG_LEVEL_DBG);

/**
 * @brief Store HAS handles to NVS
 */
int has_settings_store_handles(const bt_addr_le_t *addr,
                                const struct bt_has_handles *handles)
{
	if (!addr || !handles) {
		return -EINVAL;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	/* Create settings key: "harc/device/<addr>/has_handles" */
	char key[64];
	snprintk(key, sizeof(key), "harc/device/%s/has_handles", addr_str);

	/* Store handles as binary blob */
	int err = settings_save_one(key, handles, sizeof(*handles));
	if (err) {
		LOG_ERR("Failed to store HAS handles for %s (err %d)", addr_str, err);
		return err;
	}

	LOG_INF("Stored HAS handles for %s at %s", addr_str, key);
	LOG_INF("  features: %u (ccc: %u)", handles->features_handle, handles->features_ccc_handle);
	LOG_INF("  control_point: %u (ccc: %u)", handles->control_point_handle, handles->control_point_ccc_handle);
	LOG_INF("  active_index: %u (ccc: %u)", handles->active_index_handle, handles->active_index_ccc_handle);
	return 0;
}

/* Context for settings load callback */
struct has_load_context {
	struct bt_has_handles *handles;
	bool found;
};

/* Settings load callback for HAS handles */
static int has_settings_load_cb(const char *key, size_t len, settings_read_cb read_cb,
                                 void *cb_arg, void *param)
{
	struct has_load_context *ctx = (struct has_load_context *)param;
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

	if (strcmp(name, "has_handles") == 0) {
		if (len == sizeof(struct bt_has_handles)) {
			read_cb(cb_arg, ctx->handles, sizeof(struct bt_has_handles));
			ctx->found = true;
		} else {
			LOG_WRN("Invalid HAS handles size: %zu (expected %zu)",
			        len, sizeof(struct bt_has_handles));
		}
	}

	return 0;
}

/**
 * @brief Load HAS handles from NVS
 */
int has_settings_load_handles(const bt_addr_le_t *addr,
                               struct bt_has_handles *handles)
{
	if (!addr || !handles) {
		return -EINVAL;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	/* Create settings key base for this device */
	char key_base[64];
	snprintk(key_base, sizeof(key_base), "harc/device/%s", addr_str);
	LOG_DBG("Loading HAS handles for %s at %s", addr_str, key_base);
	/* Set up context for callback */
	struct has_load_context ctx = {
		.handles = handles,
		.found = false,
	};

	/* Load settings for this device */
	int err = settings_load_subtree_direct(key_base, has_settings_load_cb, &ctx);
	if (err) {
		LOG_DBG("Failed to load settings for %s (err %d)", addr_str, err);
		return -ENOENT;
	}

	if (!ctx.found) {
		LOG_DBG("HAS handles not found for %s", addr_str);
		return -ENOENT;
	}

	LOG_INF("Loaded HAS handles for %s", addr_str);
	LOG_INF("  features: %u (ccc: %u)", handles->features_handle, handles->features_ccc_handle);
	LOG_INF("  control_point: %u (ccc: %u)", handles->control_point_handle, handles->control_point_ccc_handle);
	LOG_INF("  active_index: %u (ccc: %u)", handles->active_index_handle, handles->active_index_ccc_handle);
	return 0;
}

/**
 * @brief Clear HAS handles from NVS
 */
int has_settings_clear_handles(const bt_addr_le_t *addr)
{
	if (!addr) {
		return -EINVAL;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	/* Create settings key: "harc/device/<addr>/has_handles" */
	char key[64];
	snprintk(key, sizeof(key), "harc/device/%s/has_handles", addr_str);

	/* Delete the setting */
	int err = settings_delete(key);
	if (err) {
		LOG_ERR("Failed to clear HAS handles for %s (err %d)", addr_str, err);
		return err;
	}

	LOG_INF("Cleared HAS handles for %s", addr_str);
	return 0;
}
