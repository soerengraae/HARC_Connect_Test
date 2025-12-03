#include "power_manager.h"
#include "ble_manager.h"
#include "devices_manager.h"
#include "button_manager.h"

LOG_MODULE_REGISTER(power_manager, LOG_LEVEL_DBG);

uint8_t reset_cause;

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

void power_manager_power_off() {
    LOG_ERR("Preparing to power off the system..."); // ERR level to ensure visibility

    /**
     * Ensure the system is ready to power off:
     * - Reconfigure button interrupts to allow wake-up.
     * - Disconnect any active BLE connections.
     * - Wait a short period to ensure operations complete.
     * - Flush logs.
     * - Power off.
    */
    
    for (ssize_t i = 1; i <= 4; i++)
        button_manager_set_button_interrupt_mode(i, GPIO_INT_LEVEL_ACTIVE);

    ble_manager_disconnect_device(device_ctx[0].conn);
    ble_manager_disconnect_device(device_ctx[1].conn);

    k_sleep(K_SECONDS(10));
    LOG_ERR("... powering off now."); // ERR level to ensure visibility
    while(log_data_pending()) {
        log_process();
    }

    sys_poweroff();
}

int power_manager_init(int rc) {
    reset_cause = rc;
    LOG_INF("Power manager initialized");
    return 0;
}