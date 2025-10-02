#include "vcp_controller.h"

LOG_MODULE_REGISTER(vcp_controller, LOG_LEVEL_DBG);

/* Global state variables */
struct bt_conn *default_conn;
struct bt_vcp_vol_ctlr *vol_ctlr;
bool vcp_discovered = false;

/* VCP callback implementations */
static void vcp_state_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err,
			 uint8_t volume, uint8_t mute)
{
	if (err) {
		LOG_ERR("VCP state error (err %d)", err);
		return;
	}

	LOG_INF("VCP state - Volume: %u, Mute: %u", volume, mute);
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

	LOG_INF("VCP controller initialized");
	return 0;
}

/* Reset VCP controller state */
void vcp_controller_reset_state(void)
{
	vcp_discovered = false;
	vol_ctlr = NULL;
}

int vcp_discover(struct bt_conn *conn)
{
	int err;
	struct bt_vcp_vol_ctlr *discovered_ctlr = NULL;

	default_conn = conn;

	bt_security_t sec = bt_conn_get_security(conn);
    LOG_INF("Current security before VCP discover: %d", sec);

	err = bt_vcp_vol_ctlr_discover(conn, &discovered_ctlr);
	if (err) {
		return err;
	}

	LOG_DBG("VCP discovery initiated");
	return err;
}