#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/comparator.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/timer/system_timer.h>

extern uint8_t power_manager_wake_button;

int print_reset_cause(uint32_t reset_cause);
void power_manager_power_off();
int get_wakeup_source(void);

#endif /* POWER_MANAGER_H */