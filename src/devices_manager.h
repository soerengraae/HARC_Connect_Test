#ifndef DEVICES_MANAGER_H
#define DEVICES_MANAGER_H

#include "ble_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/settings/settings.h>

struct bonded_device_entry
{
    bt_addr_le_t addr;
    uint8_t sirk[CSIP_SIRK_SIZE];
    bool has_sirk;
    uint8_t set_rank;
    bool is_set_member;
};

struct bond_collection
{
    struct bonded_device_entry devices[CONFIG_BT_MAX_PAIRED];
    uint8_t count;
};

#define MAX_SCANNED_DEVICES 10

#define MAX_ADDRS_PER_DEVICE 4

struct scanned_device_entry
{
    bt_addr_le_t addrs[MAX_ADDRS_PER_DEVICE];
    uint8_t addr_count;
    char name[BT_NAME_MAX_LEN];
    int8_t rssi;
    sys_snode_t node;
    uint8_t rsi[6];
};

struct device_info
{
    bt_addr_le_t addr;
    // char name[BT_NAME_MAX_LEN];
    // bool connect;
    bool is_new_device; // True if not previously bonded
    bool vcp_discovered;
    bool bas_discovered;
    bool csip_discovered;
    bool has_discovered;
    // bool searching_for_pair; // True when actively searching for set pair
};

enum connection_state
{
    CONN_STATE_DISCONNECTED,
    CONN_STATE_CONNECTING,
    CONN_STATE_PAIRING,
    CONN_STATE_PAIRED,
    CONN_STATE_BONDED,
    CONN_STATE_CONNECTED,
};

struct device_context
{
    uint8_t device_id;
    struct bt_conn *conn;
    enum connection_state state;
    struct device_info info;
    struct bt_vcp_ctlr vcp_ctlr;
    struct bt_has_ctlr has_ctlr;
    struct bt_bas_ctlr bas_ctlr;
    struct ble_cmd *current_ble_cmd;
};

extern struct device_context *device_ctx;

void devices_manager_clear_all_bonds(void);
void devices_manager_update_bonded_devices_collection(void);
int devices_manager_init(void);
int devices_manager_get_bonded_devices_collection(struct bond_collection *collection);
bool devices_manager_find_bonded_entry_by_addr(const bt_addr_le_t *addr, struct bonded_device_entry *out_entry);
struct device_context *devices_manager_get_device_context_by_conn(struct bt_conn *conn);
struct device_context *devices_manager_get_device_context_by_addr(const bt_addr_le_t *addr);
struct device_context *devices_manager_get_device_context_by_id(uint8_t device_id);

/**
 * @brief Add a scanned device by address
 * @param addr Pointer to the address
 * @param rssi RSSI value
 * @return Device count on success, negative error code on failure
 */
int devices_manager_add_scanned_device(const bt_addr_le_t *addr, int8_t rssi);
int devices_manager_update_scanned_device_name(const bt_addr_le_t *addr, const char *name);
int devices_manager_add_address_to_scanned_device(struct scanned_device_entry *entry, const bt_addr_le_t *addr_new);
uint8_t devices_manager_get_scanned_device_count(void);
struct scanned_device_entry *devices_manager_get_scanned_device(uint8_t idx);
void devices_manager_clear_scanned_devices(void);
int devices_manager_select_scanned_device(uint8_t idx, struct device_info *out_info);
void devices_manager_print_scanned_devices(void);

#endif /* DEVICES_MANAGER_H */