#include "app_controller.h"
#include "ble_manager.h"
#include "devices_manager.h"
#include "csip_coordinator.h"
#include "has_controller.h"
#include "power_manager.h"
#include "button_manager.h"

LOG_MODULE_REGISTER(app_controller, LOG_LEVEL_DBG);

enum app_event_type {
	EVENT_NONE,
	EVENT_SYSTEM_READY,
	EVENT_DEVICE_CONNECTED,
	EVENT_DEVICE_READY,
	EVENT_DEVICE_DISCONNECTED,
	EVENT_CSIP_DISCOVERED,
	EVENT_CSIP_MEMBER_MATCH,
	EVENT_SCAN_TIMEOUT,
	EVENT_PAIRING_FAILED,
	EVENT_SCAN_COMPLETE,
	EVENT_BAS_DISCOVERED,
	EVENT_VCP_DISCOVERED,
	EVENT_VCP_STATE_READ,
	EVENT_HAS_DISCOVERED,
	EVENT_HAS_PRESETS_READ,
	EVENT_VOLUME_UP_BUTTON_PRESSED,
	EVENT_VOLUME_DOWN_BUTTON_PRESSED,
	EVENT_PAIR_BUTTON_PRESSED,
	EVENT_PRESET_BUTTON_PRESSED,
	EVENT_CLEAR_BONDS_BUTTON_PRESSED,
	EVENT_BONDS_CLEARED,
};

struct app_event {
	enum app_event_type type;
	uint8_t device_id;
	uint8_t error_code;
	void *data;
};

/**
 * @brief Determine connection strategy based on bonded devices
 * @return Connection strategy enum value
 */
static void determine_state(void);

K_MSGQ_DEFINE(app_event_queue, sizeof(struct app_event), 10, 4);

static uint8_t bonded_devices_count = 0;
static enum sm_state state = SM_WAKE;

void app_controller_thread(void)
{
	struct app_event evt;

	LOG_DBG("App thread started");

	state = SM_WAKE;

	while (k_msgq_get(&app_event_queue, &evt, K_FOREVER))
		;
	if (evt.type != EVENT_SYSTEM_READY) {
		LOG_ERR("Expected EVENT_SYSTEM_READY, got %d", evt.type);
		return;
	}

	while (1) {
		switch (state) {
		case SM_IDLE:
			if (button_manager_buttons_ready == false) {
				LOG_DBG("SM_IDLE: Buttons not ready, initializing buttons");
				int err = button_manager_init_buttons();
				if (err) {
					LOG_ERR("Button init failed (err %d)", err);
					continue;
				}
			}

			// Wait for an event to trigger action
			int ret = k_msgq_get(&app_event_queue, &evt, APP_CONTROLLER_ACTION_TIMEOUT);
			if (ret == -EAGAIN) {
				// Timeout, loop back to wait for event for now
				LOG_DBG("SM_IDLE: No event received, entering deep sleep");
				power_manager_prepare_power_off();
				while(k_msgq_get(&app_event_queue, &evt, K_FOREVER)); // Wait for device disconnect
				while(k_msgq_get(&app_event_queue, &evt, K_FOREVER)); // Wait for device disconnect
				power_manager_power_off();
			} else if (ret != 0) {
				LOG_ERR("Failed to get event from queue (err %d)", ret);
				continue;
			}

			switch (evt.type) {
			/**
			 * When in idle state, upon device ready, assume something went wrong and
			 * ble_manager reconnected. Proceed to discover BAS and VCP services.
			 */
			case EVENT_DEVICE_READY:
				LOG_DBG("SM_IDLE: Device %d ready, discovering BAS and VCP", evt.device_id);
				ble_cmd_bas_discover(evt.device_id, true);
				ble_cmd_vcp_discover(evt.device_id, true);
				break;

            /**
             * Volume up button pressed event
             * In single device operation, send volume up to device 0
             * In dual device operation, send volume up to both devices
             */
			case EVENT_VOLUME_UP_BUTTON_PRESSED:
				LOG_DBG("SM_IDLE: Volume up button pressed");
				if (bonded_devices_count == 1) {
					ble_cmd_vcp_volume_up(
						0,
						false); // Device ID 0 for single device operation
				} else if (bonded_devices_count == 2) {
					ble_cmd_vcp_volume_up(
						0, false); // Device ID 0 for dual device operation
					ble_cmd_vcp_volume_up(
						1, false); // Device ID 1 for dual device operation
				} else {
					LOG_WRN("No connected device to send volume up command, "
						"bonded_devices_count=%d",
						bonded_devices_count);
				}
				break;

            /**
             * Volume down button pressed event
             * In single device operation, send volume down to device 0
             * In dual device operation, send volume down to both devices
             */
			case EVENT_VOLUME_DOWN_BUTTON_PRESSED:
				LOG_DBG("SM_IDLE: Volume down button pressed");
				if (bonded_devices_count == 1) {
					ble_cmd_vcp_volume_down(
						0,
						false); // Device ID 0 for single device operation
				} else if (bonded_devices_count == 2) {
					ble_cmd_vcp_volume_down(
						0, false); // Device ID 0 for dual device operation
					ble_cmd_vcp_volume_down(
						1, false); // Device ID 1 for dual device operation
				} else {
					LOG_WRN("No connected device to send volume down command");
				}
				break;
			
			case EVENT_PRESET_BUTTON_PRESSED:
				LOG_DBG("SM_IDLE: Preset button pressed, going to next preset");
				if (bonded_devices_count == 0) {
					LOG_WRN("No connected device to send preset command");
					break;
				}

				ble_cmd_has_next_preset(0, false); // HI uses synced presets, so only send to one device
				break;

            case EVENT_PAIR_BUTTON_PRESSED:
                LOG_DBG("SM_IDLE: Pair button pressed, clearing bonds and starting first time use procedure");
				button_manager_reset_buttons();

				devices_manager_clear_all_bonds();
				while(k_msgq_get(&app_event_queue, &evt, K_FOREVER));
				if (evt.type != EVENT_BONDS_CLEARED) {
					LOG_ERR("Expected EVENT_BONDS_CLEARED after clearing bonds, got %d", evt.type);
					break;
				}
				
				devices_manager_set_device_state(&device_ctx[0], CONN_STATE_DISCONNECTING);
				ble_manager_disconnect_device(device_ctx[0].conn);
				while(k_msgq_get(&app_event_queue, &evt, K_FOREVER));
				if (evt.type != EVENT_DEVICE_DISCONNECTED) {
					LOG_ERR("Expected EVENT_DEVICE_DISCONNECTED after disconnecting, got %d", evt.type);
					break;
				}

				if (device_ctx[0].state != CONN_STATE_DISCONNECTED) {
					LOG_ERR("Device 0 not disconnected before clearing bonds as expected, current state: %d", device_ctx[0].state);
				}

				devices_manager_set_device_state(&device_ctx[1], CONN_STATE_DISCONNECTING);
				ble_manager_disconnect_device(device_ctx[1].conn);
				while(k_msgq_get(&app_event_queue, &evt, K_FOREVER));
				if (evt.type != EVENT_DEVICE_DISCONNECTED) {
					LOG_ERR("Expected EVENT_DEVICE_DISCONNECTED after disconnecting, got %d", evt.type);
					break;
				}

				if (device_ctx[1].state != CONN_STATE_DISCONNECTED) {
					LOG_ERR("Device 1 not disconnected before clearing bonds as expected, current state: %d", device_ctx[1].state);
				}

                state = SM_FIRST_TIME_USE;
                break;
            
            case EVENT_CLEAR_BONDS_BUTTON_PRESSED:
                LOG_DBG("SM_IDLE: Clear bonds button pressed, clearing all bonds");
                devices_manager_clear_all_bonds();
				while(k_msgq_get(&app_event_queue, &evt, K_FOREVER));
				if (evt.type != EVENT_BONDS_CLEARED) {
					LOG_ERR("Expected EVENT_BONDS_CLEARED after clearing bonds, got %d", evt.type);
					break;
				}

                break;

			default:
				LOG_DBG("SM_IDLE: Received unexpected event %d", evt.type);
				break;
			}
			break;

		case SM_WAKE:
			// Determine connection strategy based on bonded devices
			LOG_DBG("SM_WAKE: Determining state");
			determine_state();
			break;

		case SM_FIRST_TIME_USE:
			LOG_DBG("SM_FIRST_TIME_USE: Starting first time use procedure");
			ble_manager_start_scan_for_HIs();

			/* The ble_manager gets 60 seconds to scan for devices */
			if (k_msgq_get(&app_event_queue, &evt, K_MSEC(BT_SCAN_TIMEOUT_MS)) == 0) {
				if (evt.type == EVENT_SCAN_COMPLETE) {
					uint8_t device_count =
						devices_manager_get_scanned_device_count();
					LOG_INF("Scan complete: %d device(s) found", device_count);

					if (device_count == 0) {
						LOG_WRN("No devices found during scan");
						state = SM_IDLE;
						break;
					}

					devices_manager_print_scanned_devices();
					if (device_count == 1) {
						LOG_INF("Only one device found, selecting it "
							"automatically");
					} else {
						LOG_WRN("Multiple devices found, selecting the "
							"first one for now");
					}
				} else {
					LOG_ERR("Unexpected event %d in SM_FIRST_TIME_USE "
						"(expected EVENT_SCAN_COMPLETE)",
						evt.type);
					state = SM_IDLE;
					break;
				}
			} else {
				LOG_DBG("Timeout waiting for scan completion in SM_FIRST_TIME_USE");
				ble_manager_stop_scan_for_HIs();
				uint8_t device_count = devices_manager_get_scanned_device_count();
				if (device_count == 0) {
					LOG_WRN("No devices found during scan");
					state = SM_IDLE;
					break;
				}

				devices_manager_print_scanned_devices();
				if (device_count == 1) {
					LOG_INF("Only one device found, selecting it "
						"automatically");
				} else {
					LOG_WRN("Multiple devices found, selecting the first one "
						"for now");
				}
			}

			ble_manager_connect_to_scanned_device(
				0, 0); // Connects to first scanned device using Device 0

			/* Now wait for the device to be ready */
			if (k_msgq_get(&app_event_queue, &evt,
				       APP_CONTROLLER_PAIRING_TIMEOUT) == 0) {
				if (evt.type == EVENT_DEVICE_READY) {
					LOG_INF("[DEVICE ID %d] ready, discovering CSIP",
						evt.device_id);
				} else {
					LOG_ERR("Unexpected event %d in SM_FIRST_TIME_USE "
						"(expected EVENT_DEVICE_READY)",
						evt.type);
					state = SM_IDLE;
					break;
				}
			} else {
				LOG_ERR("Timeout waiting for first device to be ready in "
					"SM_FIRST_TIME_USE");
				state = SM_IDLE;
				break;
			}

			ble_cmd_csip_discover(evt.device_id, false);
			while (k_msgq_get(&app_event_queue, &evt, K_FOREVER))
				;
			if (evt.type != EVENT_CSIP_DISCOVERED) {
				LOG_ERR("Unexpected event %d in SM_FIRST_TIME_USE", evt.type);
				state = SM_IDLE;
				break;
			} else {
				LOG_INF("CSIP discovered for device %d, proceeding to RSI scan",
					evt.device_id);
			}

			csip_coordinator_rsi_scan_start(evt.device_id);
			while (k_msgq_get(&app_event_queue, &evt, K_FOREVER))
				;
			if (evt.type != EVENT_CSIP_MEMBER_MATCH) {
				LOG_ERR("Unexpected event %d in SM_FIRST_TIME_USE", evt.type);
				state = SM_IDLE;
			} else {
				if (evt.error_code != 0) {
					LOG_WRN("No CSIP member match found for device %d",
						evt.device_id);
					LOG_INF("Proceeding to single device operation");
					state = SM_BONDED_DEVICES;
					bonded_devices_count = 1;
					break;
				}
			}

			char addr_str[BT_ADDR_LE_STR_LEN];
			bt_addr_le_to_str(evt.data, addr_str, sizeof(addr_str));
			LOG_INF("CSIP member match (%s) found for device %d", addr_str,
				evt.device_id);
			LOG_INF("Bonding to device");

			ble_manager_connect(1, evt.data);
			if (k_msgq_get(&app_event_queue, &evt,
				       APP_CONTROLLER_PAIRING_TIMEOUT) == 0) {
				if (evt.type == EVENT_DEVICE_READY) {
					LOG_INF("[DEVICE ID %d] ready, proceeding to dual device",
						evt.device_id);

					bonded_devices_count = 2;
					state = SM_BONDED_DEVICES;
					break;
				} else {
					LOG_ERR("Unexpected event %d in SM_FIRST_TIME_USE "
						"(expected EVENT_DEVICE_READY)",
						evt.type);
				}
			} else {
				LOG_ERR("Timeout waiting for first device to be ready in "
					"SM_FIRST_TIME_USE");
			}

			state = SM_IDLE;
			break;

		case SM_BONDED_DEVICES:
			LOG_DBG("SM_BONDED_DEVICES: Managing bonded device(s)");

            if (bonded_devices_count == 0) {
                LOG_ERR("No bonded devices count set in SM_BONDED_DEVICES");
                state = SM_IDLE;
                return;
            }

			for (ssize_t i = 0; i < bonded_devices_count; i++) {
				ble_manager_establish_trusted_bond(i);

				if (k_msgq_get(&app_event_queue, &evt,
					       APP_CONTROLLER_PAIRING_TIMEOUT) == 0) {
				} else {
					LOG_ERR("Timeout waiting for device %d to be ready in SM_BONDED_DEVICES", i);
					state = SM_IDLE;
					break;
				}

				if (evt.type != EVENT_DEVICE_READY) {
					LOG_ERR("Unexpected event %d in SM_BONDED_DEVICES", evt.type);
					state = SM_IDLE;
					break;
				} else {
					LOG_INF("[DEVICE ID %d] ready after trusted bond, discovering services", evt.device_id);
				}

				ble_cmd_bas_discover(evt.device_id, false);

				while (k_msgq_get(&app_event_queue, &evt, K_FOREVER))
					;
				if (evt.type != EVENT_BAS_DISCOVERED) {
					LOG_ERR("Unexpected event %d in SM_BONDED_DEVICES",
						evt.type);
					state = SM_IDLE;
				} else {
					LOG_INF("BAS discovered for device %d, reading level",
						evt.device_id);
					ble_cmd_bas_read_level(evt.device_id, false);
				}

				ble_cmd_vcp_discover(evt.device_id, false);

				while (k_msgq_get(&app_event_queue, &evt, K_FOREVER))
					;
				if (evt.type != EVENT_VCP_DISCOVERED) {
					LOG_ERR("Unexpected event %d in SM_BONDED_DEVICES",
						evt.type);
				} else {
					LOG_INF("VCP discovered for device %d", evt.device_id);
				}

                has_controller_reset(evt.device_id);

				/**
				 * The HI uses a synchronized set of presets across all members,
				 * so only need to perform HAS discovery on one device.
				 */
                if (i == 0) {
                    ble_cmd_has_discover(evt.device_id, false);

                    while (k_msgq_get(&app_event_queue, &evt, K_FOREVER));
                    if (evt.type != EVENT_HAS_DISCOVERED) {
                        LOG_ERR("Unexpected event %d in SM_BONDED_DEVICES", evt.type);
                    } else {
						if (evt.error_code != 0) {
							LOG_WRN("HAS discovery failed for device %d",
								evt.device_id);
							if (evt.error_code == 15) {
								LOG_DBG("Attempting to discover HAS again for device %d", evt.device_id);
								ble_cmd_has_discover(evt.device_id, false);
							}
						} else {
							LOG_INF("HAS discovered for device %d", evt.device_id);
						}
                    }
                }

				ble_cmd_vcp_read_state(evt.device_id, false);
				while(k_msgq_get(&app_event_queue, &evt, K_FOREVER));
				if (evt.type != EVENT_VCP_STATE_READ) {
					LOG_ERR("Unexpected event %d in SM_BONDED_DEVICES", evt.type);
				} else {
					LOG_INF("VCP state read for device %d", evt.device_id);
				}
			}

			// LOG_DBG("Reading HAS presets for device 0");
			// /** After discovery, automatically read all presets */
    		// ble_cmd_has_read_presets(0, false);
			// while(k_msgq_get(&app_event_queue, &evt, K_FOREVER));
			// if (evt.type != EVENT_HAS_PRESETS_READ) {
			// 	LOG_ERR("Unexpected event %d in SM_BONDED_DEVICES", evt.type);
			// } else {
			// 	LOG_INF("HAS presets read for device %d", evt.device_id);
			// }

            LOG_DBG("All bonded devices managed, entering idle state");
			state = SM_IDLE;

			switch (power_manager_wake_button) {
			case VOLUME_UP_BTN_ID:
				LOG_DBG("SM_IDLE: Wake button is volume up");
				app_controller_notify_volume_up_button_pressed();
				break;
			case VOLUME_DOWN_BTN_ID:
				LOG_DBG("SM_IDLE: Wake button is volume down");
				app_controller_notify_volume_down_button_pressed();
				break;
			case PAIR_BTN_ID:
				LOG_DBG("SM_IDLE: Wake button is pair button");
				app_controller_notify_pair_button_pressed();
				break;
			case NEXT_PRESET_BTN_ID:
				LOG_DBG("SM_IDLE: Wake button is next preset");
				app_controller_notify_preset_button_pressed();
				break;
			default:
				LOG_DBG("SM_IDLE: No wake button pressed");
				break;
			}
			break;

		default:
			LOG_ERR("Unknown state %d", state);
			k_sleep(K_MSEC(1000));
			break;
		}
	}
}

static void determine_state(void)
{
	struct bond_collection collection;
	devices_manager_get_bonded_devices_collection(&collection);
    bonded_devices_count = collection.count;

	switch (bonded_devices_count) {
	case 0:
		LOG_INF("No bonded devices found, entering first time use procedure");
		state = SM_FIRST_TIME_USE;
		break;
	case 1: case 2:
		LOG_INF("One or two bonded devices found, connecting and verifying set membership");
		state = SM_BONDED_DEVICES; // One bonded device
		break;
	default:
		LOG_ERR("Illegal number of bonded devices (%d) found", bonded_devices_count);
		state = SM_IDLE; // Multiple bonded devices
		break;
	}

	return;
}

K_THREAD_DEFINE(app_controller_thread_id, 2048, app_controller_thread, NULL, NULL, NULL, 5, 0, 0);

int8_t app_controller_notify_system_ready()
{
	LOG_DBG("Notifying system ready");
	struct app_event evt = {.type = EVENT_SYSTEM_READY, .device_id = 0};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_device_connected(uint8_t device_id)
{
	LOG_DBG("Notifying device connected: device_id=%d", device_id);
	struct app_event evt = {.type = EVENT_DEVICE_CONNECTED, .device_id = device_id};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_device_disconnected(uint8_t device_id)
{
	LOG_DBG("Notifying device disconnected: device_id=%d", device_id);
	struct app_event evt = {.type = EVENT_DEVICE_DISCONNECTED, .device_id = device_id};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_device_ready(uint8_t device_id)
{
	struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
	devices_manager_set_device_state(ctx, CONN_STATE_READY);
	LOG_DBG("Notifying device ready: device_id=%d", device_id);
	struct app_event evt = {.type = EVENT_DEVICE_READY, .device_id = device_id};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_scan_complete()
{
	LOG_INF("Notifying scan complete");
	struct app_event evt = {
		.type = EVENT_SCAN_COMPLETE,
		.device_id = 0,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_csip_discovered(uint8_t device_id, int8_t err)
{
	LOG_INF("Notifying CSIP discovered: device_id=%d", device_id);
	struct app_event evt = {
		.type = EVENT_CSIP_DISCOVERED,
		.device_id = device_id,
		.error_code = err,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_csip_member_match(uint8_t device_id, int8_t err,
					       const bt_addr_le_t *addr)
{
	LOG_INF("Notifying CSIP member match: device_id=%d", device_id);
	struct app_event evt = {.type = EVENT_CSIP_MEMBER_MATCH,
				.device_id = device_id,
				.error_code = err,
				.data = (void *)addr};

	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_bas_discovered(uint8_t device_id, int err)
{
	LOG_INF("Notifying BAS discovered: device_id=%d", device_id);
	struct app_event evt = {
		.type = EVENT_BAS_DISCOVERED,
		.device_id = device_id,
		.error_code = err,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_vcp_discovered(uint8_t device_id, int err)
{
	LOG_INF("Notifying VCP discovered: device_id=%d", device_id);
	struct app_event evt = {
		.type = EVENT_VCP_DISCOVERED,
		.device_id = device_id,
		.error_code = err,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_vcp_state_read(uint8_t device_id, int err)
{
	LOG_INF("Notifying VCP state read: device_id=%d", device_id);
	struct app_event evt = {
		.type = EVENT_VCP_STATE_READ,
		.device_id = device_id,
		.error_code = err,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_volume_up_button_pressed()
{
	LOG_DBG("Notifying volume up button pressed");
	struct app_event evt = {
		.type = EVENT_VOLUME_UP_BUTTON_PRESSED,
		.device_id = 0,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_volume_down_button_pressed()
{
	LOG_DBG("Notifying volume down button pressed");
	struct app_event evt = {
		.type = EVENT_VOLUME_DOWN_BUTTON_PRESSED,
		.device_id = 0,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_pair_button_pressed()
{
	LOG_DBG("Notifying pair button pressed");
	struct app_event evt = {
		.type = EVENT_PAIR_BUTTON_PRESSED,
		.device_id = 0,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_preset_button_pressed()
{
	LOG_DBG("Notifying preset button pressed");
	struct app_event evt = {
		.type = EVENT_PRESET_BUTTON_PRESSED,
		.device_id = 0,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_clear_bonds_button_pressed()
{
	LOG_DBG("Notifying clear bonds button pressed");
	struct app_event evt = {
		.type = EVENT_CLEAR_BONDS_BUTTON_PRESSED,
		.device_id = 0,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_bonds_cleared()
{
	LOG_DBG("Notifying bonds cleared");
	struct app_event evt = {
		.type = EVENT_BONDS_CLEARED,
		.device_id = 0,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_has_discovered(uint8_t device_id, int err)
{
	LOG_DBG("Notifying HAS discovered: device_id=%d", device_id);
	struct app_event evt = {
		.type = EVENT_HAS_DISCOVERED,
		.device_id = device_id,
		.error_code = err,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_has_presets_read(uint8_t device_id, int err)
{
	LOG_DBG("Notifying HAS presets read: device_id=%d", device_id);
	struct app_event evt = {
		.type = EVENT_HAS_PRESETS_READ,
		.device_id = device_id,
		.error_code = err,
	};
	return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}