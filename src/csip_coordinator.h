#ifndef CSIP_COORDINATOR_H
#define CSIP_COORDINATOR_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/csip.h>
#include <zephyr/logging/log.h>

/* Forward declaration to avoid circular dependency */
struct device_context;

int csip_cmd_discover(uint8_t device_id);

/* Global state */
extern bool csip_discovered;

/* CSIP settings management */
int csip_settings_store_sirk(const bt_addr_le_t *addr, const uint8_t *sirk, uint8_t rank);
int csip_settings_load_sirk(const bt_addr_le_t *addr, uint8_t *sirk, uint8_t *rank);
int csip_settings_clear_device(const bt_addr_le_t *addr);

#endif /* CSIP_COORDINATOR_H */