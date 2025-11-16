#include "ble_manager.h"
#include "csip_coordinator.h"

LOG_MODULE_REGISTER(ble_manager, LOG_LEVEL_DBG);

uint8_t ble_manager_ready;

/** Initialize BLE manager
 * @brief Sets up connection callbacks, authentication, VCP controller, and battery reader
 *
 * @return 0 on success, negative error code on failure
 */
int ble_manager_init(void)
{	
	LOG_INF("BLE manager initializing");
	ble_manager_ready = 1;
	return 0;
}

void bt_ready_cb(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	ble_manager_init();
}