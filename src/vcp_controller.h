#ifndef VCP_CONTROLLER_H
#define VCP_CONTROLLER_H

#include "ble_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/vcp.h>
#include <zephyr/logging/log.h>

/* VCP command types */
enum vcp_cmd_type {
    VCP_CMD_DISCOVER,
    VCP_CMD_VOLUME_UP,
    VCP_CMD_VOLUME_DOWN,
    VCP_CMD_SET_VOLUME,
    VCP_CMD_MUTE,
    VCP_CMD_UNMUTE,
    VCP_CMD_READ_STATE,
    VCP_CMD_READ_FLAGS,
};

/* VCP command structure */
struct vcp_cmd {
    enum vcp_cmd_type type;
    uint8_t d0;  // Data parameter
    uint8_t retry_count;
    sys_snode_t node;  // For linked list
};

/* Command queue configuration */
#define VCP_CMD_QUEUE_SIZE 10
#define VCP_CMD_MAX_RETRIES 3
#define VCP_CMD_TIMEOUT_MS 5000

/* Public API */
int vcp_controller_init(void);
int vcp_cmd_discover(void);
int vcp_cmd_volume_up(void);
int vcp_cmd_volume_down(void);
int vcp_cmd_set_volume(uint8_t volume);
int vcp_cmd_mute(void);
int vcp_cmd_unmute(void);
void vcp_controller_reset(void);
void vcp_discover_start(struct connection_context *ctx);

/* Internal functions */
int vcp_discover(struct bt_conn *conn);

/* Global state */
extern struct bt_vcp_vol_ctlr *vol_ctlr;
extern struct k_work_delayable vcp_discovery_work;
extern bool vcp_discovered;
extern bool volume_direction;

#endif // VCP_CONTROLLER_H