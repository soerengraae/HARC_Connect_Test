#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/settings/settings.h>

enum connection_strategy {
    STRATEGY_NO_BONDED_DEVICES,
    STRATEGY_ONE_BONDED_DEVICE,
    STRATEGY_TWO_BONDED_DEVICES,
    STRATEGY_ILLEGAL_STATE
};

extern uint8_t strategy;

uint8_t connection_manager_init(void);

#endif /* CONNECTION_MANAGER_H */