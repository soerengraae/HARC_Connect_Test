#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

#include "ble_manager.h"
#include "vcp_controller.h"
#include "battery_reader.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    int err;

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

    err = vcp_controller_init();
	if (err) {
		LOG_ERR("VCP controller init failed (err %d)", err);
		return err;
	}

    /* Initialize Bluetooth */
    err = bt_enable(bt_ready_cb);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }

    while (1) {
        k_sleep(K_SECONDS(5));

        if (vcp_discovered && vol_ctlr) {
            LOG_DBG("Queueing VCP Volume Change");
            
            if (volume_direction) {
                ble_cmd_vcp_volume_up();
            } else {
                ble_cmd_vcp_volume_down();
            }
        }

        if (battery_discovered) {
            ble_cmd_bas_read_level();
            if (battery_level >= 0) {
                LOG_INF("Battery level: %d%%", battery_level);
            } else {
                LOG_ERR("Battery level not available");
            }
        }
    }

    return 0;
}