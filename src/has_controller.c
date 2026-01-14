#include "has_controller.h"
#include "has_settings.h"
#include "devices_manager.h"
#include "ble_manager.h"
#include "app_controller.h"
#include "display_manager.h"

LOG_MODULE_REGISTER(has_controller, LOG_LEVEL_DBG);

/* Track whether handles were loaded from cache (per device) - skip re-storing if true */
static bool handles_from_cache[CONFIG_BT_MAX_CONN];

/* Forward declarations */
static void has_discover_cb(struct bt_conn *conn, int err, struct bt_has *has,
                           enum bt_has_hearing_aid_type type,
                           enum bt_has_capabilities caps);
static void has_preset_read_rsp_cb(struct bt_has *has, int err,
                                   const struct bt_has_preset_record *record,
                                   bool is_last);
static void has_preset_switch_cb(struct bt_has *has, int err, uint8_t index);
static struct device_context *get_device_context_by_has(struct bt_has *has);

/* HAS client callbacks */
static struct bt_has_client_cb has_callbacks = {
    .discover = has_discover_cb,
    .preset_read_rsp = has_preset_read_rsp_cb,
    .preset_switch = has_preset_switch_cb,
};

/**
 * @brief Discovery callback - called when HAS discovery completes
 */
static void has_discover_cb(struct bt_conn *conn, int err, struct bt_has *has,
                           enum bt_has_hearing_aid_type type,
                           enum bt_has_capabilities caps)
{
    struct device_context *ctx = devices_manager_get_device_context_by_conn(conn);
    if (!ctx) {
        LOG_ERR("HAS discovery callback from unknown connection");
        return;
    }

    if (err || !has) {
        LOG_ERR("HAS discovery failed (err %d) [DEVICE ID %d]", err, ctx->device_id);
        app_controller_notify_has_discovered(ctx->device_id, err ? err : -ENOENT);
        ble_cmd_complete(ctx->device_id, err ? err : -ENOENT);
        return;
    }

    LOG_INF("HAS discovery complete [DEVICE ID %d]", ctx->device_id);
    LOG_INF("Hearing aid type: %d [DEVICE ID %d]", type, ctx->device_id);
    LOG_INF("Hearing aid capabilities: 0x%02X [DEVICE ID %d]", caps, ctx->device_id);

    LOG_DBG("is_new_device: %d [DEVICE ID %d]", ctx->info.is_new_device, ctx->device_id);
    ctx->info.has_discovered = true;
    ctx->has_ctlr.has = has;

    /* Only extract and cache handles if they weren't loaded from cache.
     * This avoids unnecessary stack usage from settings operations when
     * handles are already in NVS. */
    if (!handles_from_cache[ctx->device_id]) {
        /* Extract and cache handles and features to NVS for fast reconnection */
        struct bt_has_handles handles;
        int cache_err = bt_has_client_get_handles(has, &handles);
        if (cache_err == 0) {
            /* Reconstruct features byte from type and caps
             * Note: We can't access has->features directly (opaque struct),
             * so we reconstruct what we can from the callback parameters.
             * The hearing aid type occupies bits 0-1.
             * For full feature support, we'd need to read the characteristic,
             * but this gives us enough for basic operation. */
            uint8_t features = (uint8_t)type; /* Type is in bits 0-1 */

            /* If presets are supported, the control point exists.
             * For GN hearing aids with binaural type, preset sync is typically supported.
             * We set bit 2 (BT_HAS_FEAT_PRESET_SYNC_SUPP) for binaural devices. */
            if ((caps & BT_HAS_PRESET_SUPPORT) && type == BT_HAS_HEARING_AID_TYPE_BINAURAL) {
                features |= 0x04; /* BT_HAS_FEAT_PRESET_SYNC_SUPP */
            }

            /* Store handles for the current device */
            cache_err = has_settings_store_handles(&ctx->info.addr, &handles, features);
            if (cache_err == 0) {
                LOG_INF("HAS handles and features cached to NVS for current device (features=0x%02X)", features);
            } else {
                LOG_WRN("Failed to cache HAS data for current device (err %d)", cache_err);
            }

            /**
             * Store handles for all other set members
             */
            // LOG_DBG("is_new_device: %d [DEVICE ID %d]", ctx->info.is_new_device, ctx->device_id);
            // if (ctx->info.is_new_device) {
                struct bond_collection collection;
                if (devices_manager_get_bonded_devices_collection(&collection) == 0) {
                    struct bonded_device_entry current_entry;
                    if (devices_manager_find_bonded_entry_by_addr(&ctx->info.addr, &current_entry) &&
                        current_entry.is_set_member) {

                        LOG_DBG("Current device is CSIP set member, caching HAS handles for all set members");

                        for (uint8_t i = 0; i < collection.count; i++) {
                            /* Skip the current device (already stored) */
                            if (bt_addr_le_cmp(&collection.devices[i].addr, &ctx->info.addr) == 0) {
                                continue;
                            }

                            /* Only store for devices in the same CSIP set */
                            if (collection.devices[i].is_set_member &&
                                memcmp(collection.devices[i].sirk, current_entry.sirk, CSIP_SIRK_SIZE) == 0) {

                                int err = has_settings_store_handles(&collection.devices[i].addr, &handles, features);
                                if (err == 0) {
                                    char addr_str[BT_ADDR_LE_STR_LEN];
                                    bt_addr_le_to_str(&collection.devices[i].addr, addr_str, sizeof(addr_str));
                                    LOG_INF("HAS handles also cached for set member: %s", addr_str);
                                } else {
                                    LOG_WRN("Failed to cache HAS handles for set member (err %d)", err);
                                }
                            }
                        }
                    }
                }
            // }
        } else {
            LOG_WRN("Failed to extract HAS handles (err %d)", cache_err);
        }
    } else {
        LOG_DBG("Handles were loaded from cache, skipping re-storage");
    }

    app_controller_notify_has_discovered(ctx->device_id, 0);
    ble_cmd_complete(ctx->device_id, 0);
}

/**
 * @brief Preset read response callback
 */
static void has_preset_read_rsp_cb(struct bt_has *has, int err,
                                   const struct bt_has_preset_record *record,
                                   bool is_last)
{
    struct device_context *ctx = get_device_context_by_has(has);

    if (!ctx) {
        LOG_ERR("Preset read callback from unknown connection");
        return;
    }

    if (err) {
        LOG_ERR("Preset read failed (err %d) [DEVICE ID %d]", err, ctx->device_id);
        ble_cmd_complete(ctx->device_id, err);
        return;
    }

    if (!record) {
        LOG_DBG("No more presets to read [DEVICE ID %d]", ctx->device_id);
        ble_cmd_complete(ctx->device_id, err);
        return;
    }

    LOG_DBG("Storing preset information [DEVICE ID %d]", ctx->device_id);

    // Store preset information
    if (ctx->has_ctlr.preset_count < HAS_MAX_PRESETS) {
        struct has_preset_info *preset = &ctx->has_ctlr.presets[ctx->has_ctlr.preset_count];
        preset->index = record->index;
        preset->available = (record->properties & BT_HAS_PROP_AVAILABLE) != 0;
        preset->writable = (record->properties & BT_HAS_PROP_WRITABLE) != 0;

        if (record->name) {
            strncpy(preset->name, record->name, BT_HAS_PRESET_NAME_MAX - 1);
            preset->name[BT_HAS_PRESET_NAME_MAX - 1] = '\0';
        } else {
            snprintf(preset->name, BT_HAS_PRESET_NAME_MAX, "Preset %u", record->index);
        }

        LOG_INF("Preset %u: '%s' (available: %d, writable: %d)",
                record->index, preset->name, preset->available, preset->writable);

        ctx->has_ctlr.preset_count++;
    } else {
        LOG_WRN("Maximum preset count reached, ignoring preset %u", record->index);
    }

    // If this is the last preset, complete the command
    if (is_last) {
        LOG_DBG("Preset read complete, total: %u", ctx->has_ctlr.preset_count);
        app_controller_notify_has_presets_read(ctx->device_id, 0);
        ble_cmd_complete(ctx->device_id, 0);
        ctx->has_ctlr.presets_read = true;
    }
}

/**
 * @brief Preset switch callback - called when preset is changed
 */
static void has_preset_switch_cb(struct bt_has *has, int err, uint8_t index)
{
    struct device_context *ctx = get_device_context_by_has(has);

    if (!ctx) {
        LOG_ERR("Preset switch callback from unknown connection");
        return;
    }

    if (err) {
        LOG_ERR("Preset switch failed (err %d)", err);
        ble_cmd_complete(ctx->device_id, err);
        return;
    }

    ctx->has_ctlr.active_preset_index = index;

    // Find preset name for better logging
    char *preset_name = "Unknown";
    for (int i = 0; i < ctx->has_ctlr.preset_count; i++) {
        if (ctx->has_ctlr.presets[i].index == index) {
            preset_name = ctx->has_ctlr.presets[i].name;
            break;
        }
    }

    /* Update display with new preset */
    display_manager_update_preset(ctx->device_id, index, preset_name);

    if (ctx->current_ble_cmd && ctx->current_ble_cmd->type) {
        LOG_DBG("ctx->current_ble_cmd->type=%d", ctx->current_ble_cmd->type);
            if (ctx->current_ble_cmd->type == BLE_CMD_HAS_NEXT_PRESET ||
                ctx->current_ble_cmd->type == BLE_CMD_HAS_PREV_PRESET ||
                ctx->current_ble_cmd->type == BLE_CMD_HAS_SET_PRESET)
                ble_cmd_complete(ctx->device_id, 0);
    } else {
        LOG_DBG("ctx->current_ble_cmd->type is NULL");
    }
    LOG_INF("Active preset changed to %u: '%s' [DEVICE ID %d]", index, preset_name, ctx->device_id);
}

/**
 * @brief Command: Discover HAS on connected device
 */
int has_cmd_discover(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);

    if (!ctx || !ctx->conn) {
        LOG_ERR("No active connection");
        return -ENOTCONN;
    }

    if (ctx->info.has_discovered) {
        LOG_WRN("HAS already discovered");
        return -EALREADY;
    }

    /* Reset cache flag - will be set if handles are successfully loaded from cache */
    handles_from_cache[device_id] = false;

    /* Try to load cached handles and features from NVS */
    struct has_cached_data cached_data;
    int load_err = has_settings_load_handles(&ctx->info.addr, &cached_data);

    if (load_err == 0) {
        LOG_INF("Found cached HAS data, attempting to restore");
        LOG_INF("Cached features: 0x%02X", cached_data.features);
        LOG_DBG("  Hearing aid type: %u", cached_data.features & 0x03);
        LOG_DBG("  Preset sync support: %s", (cached_data.features & 0x04) ? "yes" : "no");

        /* Inject cached handles into HAS client - this allocates the client instance */
        int inject_err = bt_has_client_set_handles(ctx->conn, &cached_data.handles);

        if (inject_err != 0) {
            LOG_WRN("Failed to inject cached handles (err %d), will perform full discovery", inject_err);
            /* Clear invalid cache */
            has_settings_clear_handles(&ctx->info.addr);
        } else {
            LOG_INF("Cached handles restored successfully");
            handles_from_cache[device_id] = true;
        }
    } else {
        LOG_DBG("No cached HAS data found (err %d), performing full discovery", load_err);
    }

    /* Start discovery - will skip GATT discovery if handles were injected successfully */
    LOG_DBG("Starting HAS discovery [DEVICE ID %d]", ctx->device_id);
    return bt_has_client_discover(ctx->conn);
}

/**
 * @brief Command: Read all presets
 */
int has_cmd_read_presets(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);

    if (!ctx || !ctx->info.has_discovered) {
        LOG_ERR("HAS not discovered [DEVICE ID %d]", device_id);
        return -ENOENT;
    }

    LOG_DBG("Reading presets [DEVICE ID %d]", ctx->device_id);

    // Reset preset storage
    ctx->has_ctlr.preset_count = 0;
    memset(ctx->has_ctlr.presets, 0, sizeof(ctx->has_ctlr.presets));

    // Read starting from first preset - callback will be called for each
    return bt_has_client_presets_read(ctx->has_ctlr.has,
                                      BT_HAS_PRESET_INDEX_FIRST,
                                      HAS_MAX_PRESETS);
}

/**
 * @brief Command: Set active preset
 */
int has_cmd_set_active_preset(uint8_t device_id, uint8_t index)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    if (!ctx) {
        return -ENOENT;
    }

    if (!ctx->info.has_discovered || !ctx->has_ctlr.has) {
        LOG_ERR("HAS not discovered [DEVICE ID %d]", device_id);
        return -ENOENT;
    }

    // Validate preset index
    bool found = false;
    for (int i = 0; i < ctx->has_ctlr.preset_count; i++) {
        if (ctx->has_ctlr.presets[i].index == index && ctx->has_ctlr.presets[i].available) {
            found = true;
            break;
        }
    }

    if (!found) {
        LOG_ERR("Invalid or unavailable preset index: %u", index);
        return -EINVAL;
    }

    LOG_DBG("Setting active preset to %u [DEVICE ID %d]", index, ctx->device_id);
    /* Use sync=false to avoid EOPNOTSUPP error when features aren't populated.
     * The hearing aid will handle synchronization automatically. */
    return bt_has_client_preset_set(ctx->has_ctlr.has, index, false);
}

/**
 * @brief Command: Activate next preset
 */
int has_cmd_next_preset(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    if (!ctx) {
        return -ENOENT;
    }

    if (!ctx->info.has_discovered || !ctx->has_ctlr.has) {
        LOG_ERR("HAS not discovered [DEVICE ID %d]", device_id);
        return -ENOENT;
    }

    if (ctx->has_ctlr.preset_count == 0) {
        LOG_ERR("No presets available [DEVICE ID %d]", device_id);
        return -ENOENT;
    }

    LOG_DBG("Activating next preset [DEVICE ID %d]", device_id);
    /* Use sync=false to avoid EOPNOTSUPP error when features aren't populated.
     * The hearing aid will handle synchronization automatically. */
    return bt_has_client_preset_next(ctx->has_ctlr.has, false);
}

/**
 * @brief Command: Activate previous preset
 */
int has_cmd_prev_preset(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    
    if (!ctx || !ctx->info.has_discovered) {
        LOG_ERR("HAS not discovered [DEVICE ID %d]", device_id);
        return -ENOENT;
    }

    if (ctx->has_ctlr.preset_count == 0) {
        LOG_ERR("No presets available [DEVICE ID %d]", device_id);
        return -ENOENT;
    }

    LOG_DBG("Activating previous preset [DEVICE ID %d]", device_id);
    /* Use sync=false to avoid EOPNOTSUPP error when features aren't populated.
     * The hearing aid will handle synchronization automatically. */
    return bt_has_client_preset_prev(ctx->has_ctlr.has, false);
}

/**
 * @brief Get information about a specific preset
 */
int has_get_preset_info(uint8_t device_id, uint8_t index, struct has_preset_info *preset_out)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    if (!ctx) {
        return -ENOENT;
    }

    if (!preset_out) {
        return -EINVAL;
    }

    for (int i = 0; i < ctx->has_ctlr.preset_count; i++) {
        if (ctx->has_ctlr.presets[i].index == index) {
            memcpy(preset_out, &ctx->has_ctlr.presets[i], sizeof(struct has_preset_info));
            return 0;
        }
    }

    return -ENOENT;
}

/**
 * @brief Get active preset index
 */
int has_get_active_preset(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    if (!ctx) {
        return -ENOENT;
    }

    if (ctx->has_ctlr.active_preset_index == BT_HAS_PRESET_INDEX_NONE) {
        return -1;
    }

    return ctx->has_ctlr.active_preset_index;
}

/**
 * @brief Initialize HAS controller
 */
int has_controller_init(void)
{
    int err;

    err = bt_has_client_cb_register(&has_callbacks);
    if (err) {
        LOG_ERR("Failed to register HAS callbacks (err %d)", err);
        return err;
    }

    LOG_INF("HAS controller initialized");
    return 0;
}

/**
 * @brief Reset HAS controller state
 */
void has_controller_reset(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    if (!ctx) {
        LOG_ERR("Cannot reset HAS controller - invalid device ID %d", device_id);
        return;
    }

    ctx->info.has_discovered = false;
    memset(&ctx->has_ctlr, 0, sizeof(struct bt_has_ctlr));
    ctx->has_ctlr.active_preset_index = BT_HAS_PRESET_INDEX_NONE;
    ctx->has_ctlr.preset_count = 0;
    ctx->has_ctlr.has = NULL;
    handles_from_cache[device_id] = false;
    LOG_DBG("HAS controller state reset [DEVICE ID %d]", ctx->device_id);
}

struct device_context *get_device_context_by_has(struct bt_has *has)
{
    struct device_context *ctx = &device_ctx[0];
    
    if (ctx->has_ctlr.has == has) {
        return ctx;
    }
    ctx = &device_ctx[1];
    if (ctx->has_ctlr.has == has) {
        return ctx;
    }

    return NULL;
}