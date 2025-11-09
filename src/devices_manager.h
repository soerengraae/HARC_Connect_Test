#ifndef DEVICES_MANAGER_H
#define DEVICES_MANAGER_H

#include "ble_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/settings/settings.h>

struct bonded_device_entry {
    bt_addr_le_t addr;
    char name[BT_NAME_MAX_LEN];
    uint8_t sirk[CSIP_SIRK_SIZE];
    bool has_sirk;
    uint8_t set_rank;
    bool is_set_member;
};

struct bond_collection {
    struct bonded_device_entry devices[CONFIG_BT_MAX_PAIRED];
    uint8_t count;
};

struct device_info
{
	bt_addr_le_t addr;
	char name[BT_NAME_MAX_LEN];
	bool connect;
  bool is_new_device;  // True if not previously bonded
  bool vcp_discovered;
  bool bas_discovered;
  bool csip_discovered;
  bool searching_for_pair;  // True when actively searching for set pair
};

enum connection_state {
    CONN_STATE_DISCONNECTED,
    CONN_STATE_CONNECTING,
    CONN_STATE_PAIRING,
    CONN_STATE_BONDED
};

struct device_context {
    uint8_t device_id;
    struct bt_conn *conn;
    enum connection_state state;
    struct device_info info;
    struct bt_vcp_vol_ctlr *vol_ctlr;
    struct bt_bas_ctlr bas_ctlr;
    struct ble_cmd *current_ble_cmd;
};

extern struct device_context *device_ctx;

void devices_manager_clear_all_bonds(void);
void devices_manager_update_bonded_devices_collection(void);
int devices_manager_init(void);
int devices_manager_get_bonded_devices_collection(struct bond_collection *collection);
void devices_manager_is_conn_in_bonded_devices_collection(struct bt_conn *conn, struct bonded_device_entry **out_entry);
struct device_context *get_device_context_by_conn(struct bt_conn *conn);
struct device_context *get_device_context_by_id(uint8_t device_id);

#endif /* DEVICES_MANAGER_H */