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
 * @brief Store HAS handles and features to NVS
 */
int has_settings_store_handles(const bt_addr_le_t *addr,
                                const struct bt_has_handles *handles,
                                uint8_t features)
{
	if (!addr || !handles) {
		return -EINVAL;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	/* Create settings key: "harc/device/<addr>/has_cache" */
	char key[64];
	snprintk(key, sizeof(key), "harc/device/%s/has_cache", addr_str);

	/* Pack handles and features into cached_data structure */
	struct has_cached_data cached_data = {
		.handles = *handles,
		.features = features,
	};

	/* Store cached data as binary blob */
	int err = settings_save_one(key, &cached_data, sizeof(cached_data));
	if (err) {
		LOG_ERR("Failed to store HAS cache for %s (err %d)", addr_str, err);
		return err;
	}

	LOG_INF("Stored HAS cache for %s at %s", addr_str, key);
	LOG_INF("  features: %u (ccc: %u), features_byte: 0x%02X",
	        handles->features_handle, handles->features_ccc_handle, features);
	LOG_INF("  control_point: %u (ccc: %u)", handles->control_point_handle, handles->control_point_ccc_handle);
	LOG_INF("  active_index: %u (ccc: %u)", handles->active_index_handle, handles->active_index_ccc_handle);
	return 0;
}

/* Context for settings load callback */
struct has_load_context {
	struct has_cached_data *cached_data;
	bool found;
};

/* Settings load callback for HAS cache */
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

	/* Try new format first (has_cache with features) */
	if (strcmp(name, "has_cache") == 0) {
		if (len == sizeof(struct has_cached_data)) {
			read_cb(cb_arg, ctx->cached_data, sizeof(struct has_cached_data));
			ctx->found = true;
		} else {
			LOG_WRN("Invalid HAS cache size: %zu (expected %zu)",
			        len, sizeof(struct has_cached_data));
		}
	}
	/* Fallback: try old format (has_handles without features) for backward compatibility */
	else if (strcmp(name, "has_handles") == 0) {
		if (len == sizeof(struct bt_has_handles)) {
			read_cb(cb_arg, &ctx->cached_data->handles, sizeof(struct bt_has_handles));
			ctx->cached_data->features = 0; /* Default to no features */
			ctx->found = true;
			LOG_INF("Loaded legacy HAS handles (no features cached)");
		} else {
			LOG_WRN("Invalid HAS handles size: %zu (expected %zu)",
			        len, sizeof(struct bt_has_handles));
		}
	}

	return 0;
}

/**
 * @brief Load HAS handles and features from NVS
 */
int has_settings_load_handles(const bt_addr_le_t *addr,
                               struct has_cached_data *cached_data)
{
	if (!addr || !cached_data) {
		return -EINVAL;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	/* Create settings key base for this device */
	char key_base[64];
	snprintk(key_base, sizeof(key_base), "harc/device/%s", addr_str);
	LOG_DBG("Loading HAS cache for %s at %s", addr_str, key_base);

	/* Set up context for callback */
	struct has_load_context ctx = {
		.cached_data = cached_data,
		.found = false,
	};

	/* Load settings for this device */
	int err = settings_load_subtree_direct(key_base, has_settings_load_cb, &ctx);
	if (err) {
		LOG_DBG("Failed to load settings for %s (err %d)", addr_str, err);
		return -ENOENT;
	}

	if (!ctx.found) {
		LOG_DBG("HAS cache not found for %s", addr_str);
		return -ENOENT;
	}

	LOG_INF("Loaded HAS cache for %s", addr_str);
	LOG_INF("  features: %u (ccc: %u), features_byte: 0x%02X",
	        cached_data->handles.features_handle, cached_data->handles.features_ccc_handle,
	        cached_data->features);
	LOG_INF("  control_point: %u (ccc: %u)",
	        cached_data->handles.control_point_handle, cached_data->handles.control_point_ccc_handle);
	LOG_INF("  active_index: %u (ccc: %u)",
	        cached_data->handles.active_index_handle, cached_data->handles.active_index_ccc_handle);
	return 0;
}

/**
 * @brief Clear HAS cache from NVS
 */
int has_settings_clear_handles(const bt_addr_le_t *addr)
{
	if (!addr) {
		return -EINVAL;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	/* Delete new format cache */
	char key_cache[64];
	snprintk(key_cache, sizeof(key_cache), "harc/device/%s/has_cache", addr_str);
	int err = settings_delete(key_cache);

	/* Also delete old format for backward compatibility */
	char key_handles[64];
	snprintk(key_handles, sizeof(key_handles), "harc/device/%s/has_handles", addr_str);
	int err2 = settings_delete(key_handles);

	/* Report error only if both deletions failed */
	if (err && err2) {
		LOG_ERR("Failed to clear HAS cache for %s (err %d)", addr_str, err);
		return err;
	}

	LOG_INF("Cleared HAS cache for %s", addr_str);
	return 0;
}
