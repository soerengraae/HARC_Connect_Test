#include "ble_manager.h"
#include "vcp_controller.h"
#include "csip_coordinator.h"
#include "devices_manager.h"
#include "battery_reader.h"
#include "app_controller.h"
#include "has_controller.h"
#include "display_manager.h"
#include "power_manager.h"
#include "button_manager.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    int err;
    uint32_t reset_cause;
    int rc;

    hwinfo_get_reset_cause(&reset_cause);
	rc = print_reset_cause(reset_cause);
    if (rc < 0) {
		LOG_DBG("Reset cause not supported.\n");
		return 0;
	}

    // /* Initialize work queue for display updates */
    // k_work_init(&display_update_work, display_update_work_handler);

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        err = settings_subsys_init();
        if (err) {
            LOG_ERR("Settings init failed (err %d)", err);
        }

        err = settings_load();
        if (err) {
            LOG_ERR("Settings load failed (err %d)", err);
        }
    }

    err = power_manager_init(rc);
    if (err) {
        LOG_ERR("Power manager init failed (err %d)", err);
        return err;
    }

    err = display_manager_init();
    if (err) {
        LOG_WRN("Display manager init failed (err %d) - continuing without display", err);
    }
    
    err = vcp_controller_init();
	if (err) {
		LOG_ERR("VCP controller init failed (err %d)", err);
		return err;
	}

    err = battery_reader_init();
	if (err)
	{
		LOG_ERR("Battery reader init failed (err %d)", err);
		return err;
	}

	err = csip_coordinator_init();
	if (err)
	{
		LOG_ERR("CSIP coordinator init failed (err %d)", err);
		return err;
	}

    err = has_controller_init();
    if (err) {
        LOG_ERR("HAS controller init failed (err %d)", err);
        return err;
    }

    /* Initialize Bluetooth */
    err = bt_enable(bt_ready_cb);

    while (1) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}