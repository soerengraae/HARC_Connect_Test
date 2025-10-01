#ifndef VCP_CONTROLLER_H
#define VCP_CONTROLLER_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/vcp.h>
#include <zephyr/logging/log.h>

extern struct bt_conn *default_conn;
extern struct bt_vcp_vol_ctlr *vol_ctlr;
extern bool vcp_discovered;

int vcp_controller_init(void);
int vcp_discover(struct bt_conn *conn);

#endif // VCP_CONTROLLER_H