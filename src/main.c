#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

#include "ble_manager.h"
#include "vcp_controller.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

int main(void)
{
	int err;

	/* Initialize Bluetooth */
	err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	while (1) {
		k_sleep(K_SECONDS(5));

		if (vcp_discovered && vol_ctlr) {
			LOG_DBG("Attempting to read VCP state (vol_ctlr=%p, default_conn=%p)",
				vol_ctlr, default_conn);

			// Read the current volume state
			int state_err = bt_vcp_vol_ctlr_read_state(vol_ctlr);
			if (state_err) {
				LOG_ERR("Failed to read VCP state (err %d)", state_err);
			} else {
				LOG_DBG("Read state initiated successfully");
			}
		}
	}

	return 0;
}