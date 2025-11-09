#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/settings/settings.h>

uint8_t connection_manager_init(void);

#endif /* CONNECTION_MANAGER_H */