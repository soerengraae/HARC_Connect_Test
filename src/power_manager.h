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

#define NON_WAKEUP_RESET_REASON (RESET_PIN | RESET_SOFTWARE | RESET_POR | RESET_DEBUG)

extern uint8_t reset_cause;

int print_reset_cause(uint32_t reset_cause);
void power_manager_power_off();
int power_manager_init(int rc);

#endif /* POWER_MANAGER_H */