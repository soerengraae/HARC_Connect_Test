#ifndef CSIP_COORDINATOR_H
#define CSIP_COORDINATOR_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/csip.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

struct device_context;

int csip_cmd_discover(uint8_t device_id);

/* Global state */
extern bool csip_discovered;

/* CSIP settings management */
int csip_settings_store_sirk(const bt_addr_le_t *addr, const uint8_t *sirk, uint8_t rank);
int csip_settings_load_sirk(const bt_addr_le_t *addr, uint8_t *sirk, uint8_t *rank);
int csip_settings_clear_device(const bt_addr_le_t *addr);

/* CSIP coordinator functions */
bool csip_get_sirk(uint8_t device_id, uint8_t *sirk_out, uint8_t *rank_out);
bool csip_verify_devices_are_set();
int csip_coordinator_init(void);
void rsi_scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                     struct net_buf_simple *ad);
void csip_coordinator_rsi_scan_start(uint8_t device_id);
uint8_t csip_get_set_size(uint8_t device_id);

#endif /* CSIP_COORDINATOR_H */