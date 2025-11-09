#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

#include "ble_manager.h"
#include "vcp_controller.h"
#include "devices_manager.h"

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

    // ble_cmd_bas_read_level(0, false);

    while (1) {
        k_sleep(K_SECONDS(5));

        if (device_ctx[0].info.vcp_discovered) {
            LOG_DBG("Queueing VCP Volume Change");
            if (volume_direction) {
                ble_cmd_vcp_volume_up(0, false);
            } else {
                ble_cmd_vcp_volume_down(0, false);
            }
        }
    }

    return 0;
}