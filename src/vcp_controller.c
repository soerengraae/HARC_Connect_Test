#include "vcp_controller.h"

LOG_MODULE_REGISTER(vcp_controller, LOG_LEVEL_DBG);

/* Global state variables */
struct bt_conn *default_conn;
struct bt_vcp_vol_ctlr *vol_ctlr;
struct bt_conn *pending_vcp_conn = NULL;
struct k_work_delayable vcp_discovery_work;
bool vcp_discovered = false;
bool volume_direction = true; // true = up, false = down

void vcp_discover_start(struct connection_context *ctx)
{
    if (ctx->state != CONN_STATE_BONDED) {
        LOG_WRN("Not starting VCP - wrong state: %d", ctx->state);
        return;
    }

    ctx->state = CONN_STATE_READY;

    if (!vcp_discovered) {
        int vcp_err = vcp_discover(ctx->conn);
        if (vcp_err) {
            LOG_ERR("VCP discovery failed (err %d)", vcp_err);
        }
    }
}

void vcp_volume_up(void)
{
	if (!vcp_discovered || !vol_ctlr) {
		LOG_WRN("VCP not discovered, cannot volume up");
		return;
	}

	int err = bt_vcp_vol_ctlr_vol_up(vol_ctlr);
	if (err) {
		LOG_ERR("Failed to initiate volume up (err %d)", err);
	} else {
		LOG_DBG("Volume up initiated");
	}
}

void vcp_volume_down(void)
{
	if (!vcp_discovered || !vol_ctlr) {
		LOG_WRN("VCP not discovered, cannot volume down");
		return;
	}

	int err = bt_vcp_vol_ctlr_vol_down(vol_ctlr);
	if (err) {
		LOG_ERR("Failed to initiate volume down (err %d)", err);
	} else {
		LOG_DBG("Volume down initiated");
	}
}

/* VCP callback implementations */
static void vcp_state_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err,
			 uint8_t volume, uint8_t mute)
{
	if (err) {
		LOG_ERR("VCP state error (err %d)", err);
		return;
	}
	// Volume is 0-255, convert to percentage
	float volume_percent = (float)volume * 100.0f / 255.0f;
	LOG_INF("VCP state - Volume: %u, Mute: %u", (uint8_t)(volume_percent), mute);

	if (volume >= 255)
	{
		volume_direction = false; // Switch to volume down
	}
	else if (volume <= 0)
	{
		volume_direction = true; // Switch to volume up
	}
}

static void vcp_flags_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err, uint8_t flags)
{
	if (err) {
		LOG_ERR("VCP flags error (err %d)", err);
		return;
	}

	LOG_INF("VCP flags: 0x%02X", flags);
}

static void vcp_discover_cb(struct bt_vcp_vol_ctlr *vcp_vol_ctlr, int err,
			    uint8_t vocs_count, uint8_t aics_count)
{
	if (err) {
		LOG_ERR("VCP discovery failed (err %d)", err);
		return;
	}

	LOG_INF("VCP discovery complete - VOCS: %u, AICS: %u", vocs_count, aics_count);

	vol_ctlr = vcp_vol_ctlr;
	vcp_discovered = true;
}

static void vcp_vol_down_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
	if (err) {
		LOG_ERR("VCP volume down error (err %d)", err);
		return;
	}

	LOG_INF("Volume down success");
}

static void vcp_vol_up_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
	if (err) {
		LOG_ERR("VCP volume up error (err %d)", err);
		return;
	}

	LOG_INF("Volume up success");
}

static void vcp_mute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
	if (err) {
		LOG_ERR("VCP mute error (err %d)", err);
		return;
	}

	LOG_INF("Mute success");
}

static void vcp_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
	if (err) {
		LOG_ERR("VCP unmute error (err %d)", err);
		return;
	}

	LOG_INF("Unmute success");
}

static void vcp_vol_up_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
	if (err) {
		LOG_ERR("VCP volume up and unmute error (err %d)", err);
		return;
	}

	LOG_INF("Volume up and unmute success");
}

static void vcp_vol_down_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
	if (err) {
		LOG_ERR("VCP volume down and unmute error (err %d)", err);
		return;
	}

	LOG_INF("Volume down and unmute success");
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
	.vol_set = NULL, // Not implemented
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

	vcp_controller_reset_state();

	LOG_INF("VCP controller initialized");

	return 0;
}

/* Reset VCP controller state */
void vcp_controller_reset_state(void)
{
	vcp_discovered = false;
	vol_ctlr = NULL;
	default_conn = NULL;
}

int vcp_discover(struct bt_conn *conn)
{
	int err;
	struct bt_vcp_vol_ctlr *discovered_ctlr = NULL;

	default_conn = conn;

	bt_security_t sec = bt_conn_get_security(conn);
	LOG_DBG("Current security before VCP discover: %d", sec);

	err = bt_vcp_vol_ctlr_discover(conn, &discovered_ctlr);
	if (err) {
		return err;
	}

	LOG_DBG("VCP discovery initiated");
	return err;
}