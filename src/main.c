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
                vcp_cmd_volume_up();
            } else {
                vcp_cmd_volume_down();
            }
        }

        if (battery_discovered) {
            int level = battery_get_level();
            if (level >= 0) {
                LOG_INF("Battery level: %d%%", level);
            } else {
                LOG_ERR("Battery level not available");
            }
        }
    }

    return 0;
}