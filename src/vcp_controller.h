#ifndef VCP_CONTROLLER_H
#define VCP_CONTROLLER_H

#include "ble_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/vcp.h>
#include <zephyr/logging/log.h>

int vcp_controller_init(void);
int vcp_cmd_discover(void);
int vcp_cmd_volume_up(void);
int vcp_cmd_volume_down(void);
int vcp_cmd_set_volume(uint8_t volume);
int vcp_cmd_mute(void);
int vcp_cmd_unmute(void);
int vcp_cmd_read_state(void);
int vcp_cmd_read_flags(void);
void vcp_controller_reset(void);

/* Global state */
extern struct bt_vcp_vol_ctlr *vol_ctlr;
extern struct k_work_delayable vcp_discovery_work;
extern bool vcp_discovered;
extern bool volume_direction;

#endif // VCP_CONTROLLER_H