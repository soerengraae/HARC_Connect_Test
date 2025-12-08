#include "power_manager.h"
#include "ble_manager.h"
#include "devices_manager.h"
#include "button_manager.h"
#include "display_manager.h"
#include <hal/nrf_gpio.h>
#include <zephyr/init.h>

LOG_MODULE_REGISTER(power_manager, LOG_LEVEL_DBG);

uint8_t power_manager_wake_button;

SYS_INIT(get_wakeup_source, PRE_KERNEL_1, 0);

int get_wakeup_source(void) {
    uint32_t reset_cause;
    hwinfo_get_reset_cause(&reset_cause);
    power_manager_wake_button = 0;

    // Check which button woke us up
    if (nrf_gpio_pin_latch_get(VOLUME_UP_BTN_PIN)) {
        // Volume up pressed
        nrf_gpio_pin_latch_clear(VOLUME_UP_BTN_PIN);
        power_manager_wake_button = VOLUME_UP_BTN_ID;
    } else if (nrf_gpio_pin_latch_get(VOLUME_DOWN_BTN_PIN)) {
        // Volume down pressed
        nrf_gpio_pin_latch_clear(VOLUME_DOWN_BTN_PIN);
        power_manager_wake_button = VOLUME_DOWN_BTN_ID;
    } else if (nrf_gpio_pin_latch_get(PAIR_BTN_PIN)) {
        // Pair button pressed
        nrf_gpio_pin_latch_clear(PAIR_BTN_PIN);
        power_manager_wake_button = PAIR_BTN_ID;
    } else if (nrf_gpio_pin_latch_get(NEXT_PRESET_BTN_PIN)) {
        // Next preset pressed
        nrf_gpio_pin_latch_clear(NEXT_PRESET_BTN_PIN);
        power_manager_wake_button = NEXT_PRESET_BTN_ID;
    }

    return 0;
}

int print_reset_cause(uint32_t reset_cause)
{
	int32_t ret;
	uint32_t supported;

	ret = hwinfo_get_supported_reset_cause((uint32_t *) &supported);

	if (ret || !(reset_cause & supported)) {
		return -ENOTSUP;
	}

	if (reset_cause & RESET_DEBUG) {
		LOG_DBG("Reset by debugger.");
	} else if (reset_cause & RESET_LOW_POWER_WAKE) {
		LOG_DBG("Wakeup from System OFF by GPIO.");
	} else  if (reset_cause & RESET_SOFTWARE) {
		LOG_DBG("Software reset.");
	} else if (reset_cause & RESET_PIN) {
        LOG_DBG("Reset by external pin.");
    } else if (reset_cause & RESET_POR) {
        LOG_DBG("Power-on reset.");
    } else {
        LOG_DBG("Reset by other cause(s): 0x%08X", reset_cause & supported);
    }

	return 0;
}

void power_manager_prepare_power_off() {
    int err;

    LOG_ERR("Preparing to power off the system..."); // ERR level to ensure visibility

    /**
     * Ensure the system is ready to power off:
     * - Put display to sleep.
     * - Reconfigure button interrupts to allow wake-up.
     * - Disconnect any active BLE connections.
     * - Wait a short period to ensure operations complete.
     * - Flush logs.
     * - Power off.
    */

    err = display_manager_sleep();
    if (err) {
        LOG_WRN("Failed to sleep display (err %d) - continuing", err);
    }

    for (ssize_t i = 1; i <= 4; i++)
        button_manager_set_button_interrupt_mode(i, GPIO_INT_LEVEL_ACTIVE);

    if (ble_manager_disconnect_device(device_ctx[0].conn) == -EINVAL) {
        LOG_DBG("No active connection to disconnect for device 0");
        app_controller_notify_device_disconnected(0);
    }
    if (ble_manager_disconnect_device(device_ctx[1].conn) == -EINVAL) {
        LOG_DBG("No active connection to disconnect for device 1");
        app_controller_notify_device_disconnected(1);
    }
}

void power_manager_power_off() {
    LOG_ERR("... powering off now."); // ERR level to ensure visibility
    while(log_data_pending()) {
        log_process();
    }
    sys_poweroff();
    sys_reboot(SYS_REBOOT_COLD); // Fallback in case poweroff fails
}