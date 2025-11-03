#ifndef VCP_CONTROLLER_H
#define VCP_CONTROLLER_H

#include "ble_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/vcp.h>
#include <zephyr/logging/log.h>

int vcp_controller_init(void);
int vcp_cmd_discover(uint8_t device_id);
int vcp_cmd_volume_up(uint8_t device_id);
int vcp_cmd_volume_down(uint8_t device_id);
int vcp_cmd_set_volume(uint8_t device_id, uint8_t volume);
int vcp_cmd_mute(uint8_t device_id);
int vcp_cmd_unmute(uint8_t device_id);
int vcp_cmd_read_state(uint8_t device_id);
int vcp_cmd_read_flags(uint8_t device_id);
void vcp_controller_reset(uint8_t device_id);

/* Global state */
extern bool volume_direction;

#endif // VCP_CONTROLLER_H