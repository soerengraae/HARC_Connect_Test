#include "vcp_controller.h"
#include "devices_manager.h"
#include "app_controller.h"
#include "ble_manager.h"
#include "display_manager.h"

LOG_MODULE_REGISTER(vcp_controller, LOG_LEVEL_DBG);

static struct device_context *get_device_context_by_vol_ctlr(struct bt_vcp_vol_ctlr *vol_ctlr);

int vcp_cmd_discover(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    vcp_controller_reset(device_id);
    return bt_vcp_vol_ctlr_discover(ctx->conn, &ctx->vcp_ctlr.vol_ctlr);
}

int vcp_cmd_read_state(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_read_state(ctx->vcp_ctlr.vol_ctlr);
}

int vcp_cmd_read_flags(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_read_flags(ctx->vcp_ctlr.vol_ctlr);
}

int vcp_cmd_volume_up(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_vol_up(ctx->vcp_ctlr.vol_ctlr);
}

int vcp_cmd_volume_down(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_vol_down(ctx->vcp_ctlr.vol_ctlr);
}

int vcp_cmd_set_volume(uint8_t device_id, uint8_t volume)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_set_vol(ctx->vcp_ctlr.vol_ctlr, volume);
}

int vcp_cmd_mute(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_mute(ctx->vcp_ctlr.vol_ctlr);
}

int vcp_cmd_unmute(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_unmute(ctx->vcp_ctlr.vol_ctlr);
}

static void vcp_state_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err, uint8_t volume, uint8_t mute)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);
    if (err) {
        LOG_ERR("VCP state error (err %d) [DEVICE ID %d]", err, ctx->device_id);
        ble_cmd_complete(ctx->device_id, err);
        return;
    }

    ctx->vcp_ctlr.state.volume = volume;
    ctx->vcp_ctlr.state.mute = mute;

    float volume_percent = (float)ctx->vcp_ctlr.state.volume * 100.0f / 255.0f;
    if (ctx->current_ble_cmd && ctx->current_ble_cmd->type == BLE_CMD_VCP_READ_STATE) {
        LOG_INF("VCP state read: Volume: %u%%, Mute: %u [DEVICE ID %d]", (uint8_t)(volume_percent), ctx->vcp_ctlr.state.mute, ctx->device_id);
    } else {
        LOG_DBG("VCP state notification: Volume: %u%%, Mute: %u [DEVICE ID %d]", (uint8_t)(volume_percent), ctx->vcp_ctlr.state.mute, ctx->device_id);
    }

    /* Update display with current volume state */
    display_manager_update_volume(ctx->device_id, volume, mute);

    // Mark as complete only if this was a READ_STATE command
    if (ctx->current_ble_cmd && ctx->current_ble_cmd->type == BLE_CMD_VCP_READ_STATE) {
        app_controller_notify_vcp_state_read(ctx->device_id, 0);
        ble_cmd_complete(ctx->device_id, 0);
    }
}

static void vcp_flags_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err, uint8_t flags)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);
    if (err) {
        LOG_ERR("VCP flags error (err %d) [DEVICE ID %d]", err, ctx->device_id);
        ble_cmd_complete(ctx->device_id, err);
        return;
    }

    LOG_DBG("VCP flags: 0x%02X [DEVICE ID %d]", flags, ctx->device_id);
    
    // Mark as complete only if this was a READ_FLAGS command as it could also be a notification
    // in which case we don't want to accidentally complete a different command
    if (ctx->current_ble_cmd && ctx->current_ble_cmd->type == BLE_CMD_VCP_READ_FLAGS) {
        ble_cmd_complete(ctx->device_id, 0);
    }
}

static void vcp_discover_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err,
                uint8_t vocs_count, uint8_t aics_count)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);

    if (err) {
        LOG_ERR("VCP discovery failed (err %d) [DEVICE ID %d]", err, ctx->device_id);
        app_controller_notify_vcp_discovered(ctx->device_id, err);
        ble_cmd_complete(ctx->device_id, err);
        return;
    }

    LOG_INF("VCP discovery complete [DEVICE ID %d]", ctx->device_id);

    ctx->vcp_ctlr.vol_ctlr = vol_ctlr;
    ctx->info.vcp_discovered = true;

    // Initial flag read
    // ble_cmd_vcp_read_flags(ctx->device_id, true);

	// Mark discovery command as complete
    app_controller_notify_vcp_discovered(ctx->device_id, err);
	ble_cmd_complete(ctx->device_id, 0);
}

static void vcp_vol_down_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);

    if (err) {
        LOG_ERR("VCP volume down error (err %d) [DEVICE ID %d]", err, ctx->device_id);
    } else {
        LOG_INF("Volume down success [DEVICE ID %d]", ctx->device_id);
    }
    
    ble_cmd_complete(ctx->device_id, err);
}

static void vcp_vol_up_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);

    if (err) {
        LOG_ERR("VCP volume up error (err %d) [DEVICE ID %d]", err, ctx->device_id);
    } else {
        LOG_INF("Volume up success [DEVICE ID %d]", ctx->device_id);
    }

    ble_cmd_complete(ctx->device_id, err);
}

static void vcp_mute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);
    
    if (err) {
        LOG_ERR("VCP mute error (err %d) [DEVICE ID %d]", err, ctx->device_id);
    } else {
        LOG_INF("Mute success [DEVICE ID %d]", ctx->device_id);
    }

    ble_cmd_complete(ctx->device_id, err);
}

static void vcp_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);

    if (err) {
        LOG_ERR("VCP unmute error (err %d) [DEVICE ID %d]", err, ctx->device_id);
    } else {
        LOG_INF("Unmute success [DEVICE ID %d]", ctx->device_id);
    }

    ble_cmd_complete(ctx->device_id, err);
}

static void vcp_vol_up_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);

    if (err) {
        LOG_ERR("VCP volume up and unmute error (err %d) [DEVICE ID %d]", err, ctx->device_id);
    } else {
        LOG_INF("Volume up and unmute success [DEVICE ID %d]", ctx->device_id);
    }

    ble_cmd_complete(ctx->device_id, err);
}

static void vcp_vol_down_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);
    
    if (err) {
        LOG_ERR("VCP volume down and unmute error (err %d) [DEVICE ID %d]", err, ctx->device_id);
    } else {
        LOG_INF("Volume down and unmute success [DEVICE ID %d]", ctx->device_id);
    }

    ble_cmd_complete(ctx->device_id, err);
}

static struct bt_vcp_vol_ctlr_cb vcp_callbacks = {
    .state = vcp_state_cb,
    .flags = vcp_flags_cb,
    .discover = vcp_discover_cb,
    .vol_down = vcp_vol_down_cb,
    .vol_up = vcp_vol_up_cb,
    .mute = vcp_mute_cb,
    .unmute = vcp_unmute_cb,
    .vol_up_unmute = vcp_vol_up_unmute_cb,
    .vol_down_unmute = vcp_vol_down_unmute_cb,
    .vol_set = NULL,
};

/* Initialize VCP controller */
int vcp_controller_init(void)
{
    int err;

    err = bt_vcp_vol_ctlr_cb_register(&vcp_callbacks);
    if (err) {
        LOG_ERR("Failed to register VCP callbacks (err %d)", err);
        return err;
    }

    LOG_INF("VCP controller initialized");

    return 0;
}

/* Reset VCP controller state */
void vcp_controller_reset(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);

    ctx->info.vcp_discovered = false;
    ctx->vcp_ctlr.vol_ctlr = NULL;

    LOG_DBG("VCP controller state reset [DEVICE ID %d]", ctx->device_id);
}

static struct device_context *get_device_context_by_vol_ctlr(struct bt_vcp_vol_ctlr *vol_ctlr)
{
    struct bt_conn *conn = NULL;
    bt_vcp_vol_ctlr_conn_get(vol_ctlr, &conn);

    return devices_manager_get_device_context_by_conn(conn);
}