#ifndef CSIP_COORDINATOR_H
#define CSIP_COORDINATOR_H

#include "ble_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/csip.h>
#include <zephyr/logging/log.h>

int csip_cmd_discover(uint8_t device_id);

/* Global state */
extern bool csip_discovered;

#endif /* CSIP_COORDINATOR_H */