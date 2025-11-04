#ifndef CSIP_COORDINATOR_H
#define CSIP_COORDINATOR_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/csip.h>
#include <zephyr/logging/log.h>

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
bool csip_verify_set_membership(uint8_t device_id_1, uint8_t device_id_2);
int csip_coordinator_init(void);

#endif /* CSIP_COORDINATOR_H */