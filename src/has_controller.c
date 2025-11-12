#include "has_controller.h"

LOG_MODULE_REGISTER(has_controller, LOG_LEVEL_INF);

struct bt_has *has_client = NULL;
bool has_discovered = false;
uint8_t active_preset_index = BT_HAS_PRESET_INDEX_NONE;
uint8_t preset_count = 0;
struct has_preset_info presets[HAS_MAX_PRESETS];

/* Forward declarations */
static void has_discover_cb(struct bt_conn *conn, int err, struct bt_has *has,
                           enum bt_has_hearing_aid_type type,
                           enum bt_has_capabilities caps);
static void has_preset_read_rsp_cb(struct bt_has *has, int err,
                                   const struct bt_has_preset_record *record,
                                   bool is_last);
static void has_preset_switch_cb(struct bt_has *has, int err, uint8_t index);

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
    if (err || !has) {
        LOG_ERR("HAS discovery failed (err %d)", err);
        ble_cmd_complete(err ? err : -ENOENT);
        return;
    }

    LOG_INF("HAS discovery complete");
    LOG_INF("Hearing aid type: %d", type);
    LOG_INF("Hearing aid capabilities: 0x%02X", caps);

    has_client = has;
    has_discovered = true;

    // After discovery, automatically read all presets
    ble_cmd_has_read_presets(true);

    ble_cmd_complete(0);
}

/**
 * @brief Preset read response callback
 */
static void has_preset_read_rsp_cb(struct bt_has *has, int err,
                                   const struct bt_has_preset_record *record,
                                   bool is_last)
{
    if (err) {
        LOG_ERR("Preset read failed (err %d)", err);
        ble_cmd_complete(err);
        return;
    }

    if (!record) {
        LOG_DBG("No more presets");
        ble_cmd_complete(0);
        return;
    }

    // Store preset information
    if (preset_count < HAS_MAX_PRESETS) {
        struct has_preset_info *preset = &presets[preset_count];
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

        preset_count++;
    } else {
        LOG_WRN("Maximum preset count reached, ignoring preset %u", record->index);
    }

    // If this is the last preset, complete the command
    if (is_last) {
        LOG_DBG("Preset read complete, total: %u", preset_count);
        ble_cmd_complete(0);
    }
}

/**
 * @brief Preset switch callback - called when preset is changed
 */
static void has_preset_switch_cb(struct bt_has *has, int err, uint8_t index)
{
    if (err) {
        LOG_ERR("Preset switch failed (err %d)", err);
        ble_cmd_complete(err);
        return;
    }

    active_preset_index = index;

    // Find preset name for better logging
    const char *preset_name = "Unknown";
    for (int i = 0; i < preset_count; i++) {
        if (presets[i].index == index) {
            preset_name = presets[i].name;
            break;
        }
    }

    LOG_INF("Active preset changed to %u: '%s'", index, preset_name);
    ble_cmd_complete(0);
}

/**
 * @brief Command: Discover HAS on connected device
 */
int has_cmd_discover(void)
{
    if (!current_conn_ctx || !current_conn_ctx->conn) {
        LOG_ERR("No active connection");
        return -ENOTCONN;
    }

    if (has_discovered) {
        LOG_WRN("HAS already discovered");
        return -EALREADY;
    }

    LOG_DBG("Starting HAS discovery");
    return bt_has_client_discover(current_conn_ctx->conn);
}

/**
 * @brief Command: Read all presets
 */
int has_cmd_read_presets(void)
{
    if (!has_discovered || !has_client) {
        LOG_ERR("HAS not discovered");
        return -ENOENT;
    }

    LOG_DBG("Reading presets from index 1");

    // Reset preset storage
    preset_count = 0;
    memset(presets, 0, sizeof(presets));

    // Read starting from first preset - callback will be called for each
    return bt_has_client_presets_read(has_client,
                                      BT_HAS_PRESET_INDEX_FIRST,
                                      HAS_MAX_PRESETS);
}

/**
 * @brief Command: Set active preset
 */
int has_cmd_set_active_preset(uint8_t index)
{
    if (!has_discovered || !has_client) {
        LOG_ERR("HAS not discovered");
        return -ENOENT;
    }

    // Validate preset index
    bool found = false;
    for (int i = 0; i < preset_count; i++) {
        if (presets[i].index == index && presets[i].available) {
            found = true;
            break;
        }
    }

    if (!found) {
        LOG_ERR("Invalid or unavailable preset index: %u", index);
        return -EINVAL;
    }

    LOG_DBG("Setting active preset to %u", index);
    return bt_has_client_preset_set(has_client, index, true);
}

/**
 * @brief Command: Activate next preset
 */
int has_cmd_next_preset(void)
{
    if (!has_discovered || !has_client) {
        LOG_ERR("HAS not discovered");
        return -ENOENT;
    }

    if (preset_count == 0) {
        LOG_ERR("No presets available");
        return -ENOENT;
    }

    LOG_DBG("Activating next preset");
    return bt_has_client_preset_next(has_client, true);
}

/**
 * @brief Command: Activate previous preset
 */
int has_cmd_prev_preset(void)
{
    if (!has_discovered || !has_client) {
        LOG_ERR("HAS not discovered");
        return -ENOENT;
    }

    if (preset_count == 0) {
        LOG_ERR("No presets available");
        return -ENOENT;
    }

    LOG_DBG("Activating previous preset");
    return bt_has_client_preset_prev(has_client, true);
}

/**
 * @brief Get information about a specific preset
 */
int has_get_preset_info(uint8_t index, struct has_preset_info *preset_out)
{
    if (!preset_out) {
        return -EINVAL;
    }

    for (int i = 0; i < preset_count; i++) {
        if (presets[i].index == index) {
            memcpy(preset_out, &presets[i], sizeof(struct has_preset_info));
            return 0;
        }
    }

    return -ENOENT;
}

/**
 * @brief Get active preset index
 */
int has_get_active_preset(void)
{
    if (active_preset_index == BT_HAS_PRESET_INDEX_NONE) {
        return -1;
    }
    return active_preset_index;
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

    has_controller_reset();

    LOG_INF("HAS controller initialized");
    return 0;
}

/**
 * @brief Reset HAS controller state
 */
void has_controller_reset(void)
{
    has_discovered = false;
    has_client = NULL;
    active_preset_index = BT_HAS_PRESET_INDEX_NONE;
    preset_count = 0;
    memset(presets, 0, sizeof(presets));
    LOG_DBG("HAS controller state reset");
}