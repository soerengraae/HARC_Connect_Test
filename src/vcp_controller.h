#ifndef VCP_CONTROLLER_H
#define VCP_CONTROLLER_H

#include "ble_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/vcp.h>
#include <zephyr/logging/log.h>

extern struct bt_conn *default_conn;
extern struct bt_vcp_vol_ctlr *vol_ctlr;
extern struct k_work_delayable vcp_discovery_work;
extern struct bt_conn *pending_vcp_conn;
extern bool vcp_discovered;
extern bool volume_direction;

int vcp_controller_init(void);
int vcp_discover(struct bt_conn *conn);
void vcp_controller_reset_state(void);
void vcp_volume_up(void);
void vcp_volume_down(void);
void vcp_discover_start(struct connection_context *ctx);

#endif // VCP_CONTROLLER_H