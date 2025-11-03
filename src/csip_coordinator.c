#include "csip_coordinator.h"

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