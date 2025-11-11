#include "app_controller.h"
#include "ble_manager.h"
#include "devices_manager.h"
#include "csip_coordinator.h"

LOG_MODULE_REGISTER(app_controller, LOG_LEVEL_DBG);

enum app_event_type
{
    EVENT_NONE,
    EVENT_SYSTEM_READY,
    EVENT_DEVICE_CONNECTED,
    EVENT_DEVICE_READY,
    EVENT_DEVICE_DISCONNECTED,
    EVENT_BLE_CMD_COMPLETE,
    EVENT_CSIP_MEMBER_MATCH,
    EVENT_CSIP_NO_MEMBER_MATCH,
    EVENT_SCAN_TIMEOUT,
    EVENT_PAIRING_FAILED,
    EVENT_SCAN_COMPLETE,
};

struct app_event
{
    enum app_event_type type;
    uint8_t device_id;
    enum ble_cmd_type ble_cmd_type;
    uint8_t error_code;
};

/**
 * @brief Determine connection strategy based on bonded devices
 * @return Connection strategy enum value
 */
static uint8_t determine_state(void);

K_MSGQ_DEFINE(app_event_queue, sizeof(struct app_event), 10, 4);

uint8_t strategy;
static enum sm_state state = SM_WAKE;

void app_controller_thread(void)
{
    struct app_event evt;

    LOG_DBG("App thread started");

    state = SM_WAKE;

    while (k_msgq_get(&app_event_queue, &evt, K_FOREVER))
        ;
    if (evt.type != EVENT_SYSTEM_READY)
    {
        LOG_ERR("Expected EVENT_SYSTEM_READY, got %d", evt.type);
        return;
    }

    while (1)
    {
        switch (state)
        {
        case SM_IDLE:
            // Wait for an event to trigger action
            while (k_msgq_get(&app_event_queue, &evt, K_FOREVER))
                ;
            switch (evt.type)
            {
            default:
                LOG_DBG("SM_IDLE: Received event %d", evt.type);
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
            if (k_msgq_get(&app_event_queue, &evt, K_MSEC(BT_SCAN_TIMEOUT_MS)) == 0)
            {
                if (evt.type == EVENT_SCAN_COMPLETE)
                {
                    uint8_t device_count = devices_manager_get_scanned_device_count();
                    LOG_INF("Scan complete: %d device(s) found", device_count);

                    if (device_count == 0)
                    {
                        LOG_WRN("No devices found during scan");
                        state = SM_IDLE;
                        break;
                    }

                    devices_manager_print_scanned_devices();
                    if (device_count == 1)
                    {
                        LOG_INF("Only one device found, selecting it automatically");
                    }
                    else
                    {
                        LOG_WRN("Multiple devices found, selecting the first one for now");
                    }
                }
                else
                {
                    LOG_ERR("Unexpected event %d in SM_FIRST_TIME_USE (expected EVENT_SCAN_COMPLETE)", evt.type);
                    state = SM_IDLE;
                    break;
                }
            }
            else
            {
                LOG_DBG("Timeout waiting for scan completion in SM_FIRST_TIME_USE");
                ble_manager_stop_scan_for_HIs();
                uint8_t device_count = devices_manager_get_scanned_device_count();
                if (device_count == 0)
                {
                    LOG_WRN("No devices found during scan");
                    state = SM_IDLE;
                    break;
                }

                devices_manager_print_scanned_devices();
                if (device_count == 1)
                {
                    LOG_INF("Only one device found, selecting it automatically");
                }
                else
                {
                    LOG_WRN("Multiple devices found, selecting the first one for now");
                }
            }

            ble_manager_connect_to_scanned_device(0, 0); // Connects to first scanned device using Device 0

            /* Now wait for the device to be ready */
            if (k_msgq_get(&app_event_queue, &evt, K_MSEC(BT_CONNECTION_TIMEOUT_MS)) == 0)
            {
                if (evt.type == EVENT_DEVICE_READY)
                {
                    LOG_INF("[DEVICE ID %d] ready, discovering CSIP", evt.device_id);
                }
                else
                {
                    LOG_ERR("Unexpected event %d in SM_FIRST_TIME_USE (expected EVENT_DEVICE_READY)", evt.type);
                    state = SM_IDLE;
                    break;
                }
            }
            else
            {
                LOG_ERR("Timeout waiting for device connection in SM_FIRST_TIME_USE");
                state = SM_IDLE;
                break;
            }

            ble_cmd_csip_discover(evt.device_id, false);
            while (k_msgq_get(&app_event_queue, &evt, K_FOREVER));
            if (evt.type != EVENT_BLE_CMD_COMPLETE)
            {
                LOG_ERR("Unexpected event %d in SM_FIRST_TIME_USE", evt.type);
                state = SM_IDLE;
                break;
            }
            else if (evt.ble_cmd_type != BLE_CMD_CSIP_DISCOVER)
            {
                LOG_ERR("Unexpected BLE command complete type %d in SM_FIRST_TIME_USE", evt.ble_cmd_type);
                state = SM_IDLE;
                break;
            }
            else if (evt.error_code != 0)
            {
                LOG_ERR("CSIP discovery failed with error %d in SM_FIRST_TIME_USE", evt.error_code);
                state = SM_IDLE;
                break;
            }
            else
            {
                LOG_INF("CSIP discovered for device %d, proceeding to RSI scan", evt.device_id);
                csip_coordinator_rsi_scan_start(evt.device_id);
            }

            while (k_msgq_get(&app_event_queue, &evt, K_FOREVER))
                ;
            if (evt.type != EVENT_CSIP_MEMBER_MATCH)
            {
                if (evt.type == EVENT_CSIP_NO_MEMBER_MATCH)
                {
                    LOG_WRN("No CSIP member match found for device %d", evt.device_id);
                    LOG_INF("Proceeding with single device operation");
                }
                else
                {
                    LOG_ERR("Unexpected event %d in SM_FIRST_TIME_USE", evt.type);
                }
                state = SM_IDLE;
                break;
            }
            else
            {
                LOG_INF("CSIP member match found for device %d, both devices ready", evt.device_id);
                state = SM_IDLE;
            }

            break;

        default:
            LOG_ERR("Unknown connection state %d", state);
            k_sleep(K_MSEC(1000));
            break;
        }
    }
}

static uint8_t determine_state(void)
{
    struct bond_collection collection;
    devices_manager_get_bonded_devices_collection(&collection);

    switch (collection.count)
    {
    case 0:
        LOG_INF("No bonded devices found");
        LOG_INF("HARC HI Remote will now:");
        LOG_INF(" 1. Scan for nearby HARC HI devices");
        LOG_INF(" 2. Connect to the first device found");
        LOG_INF(" 3. Pair and bond automatically");
        LOG_INF(" 4. Discover CSIP services and characteristics");
        LOG_INF(" 5. Store SIRK and rank for automatic reconnection next time");
        LOG_INF(" 6. Scan for RSI advertisements, resolve RSI, connect if match, discover SIRK, compare SIRK, and bond if match");
        LOG_INF(" 7. Discover other services and characteristics for both devices");
        LOG_INF(" 8. Ready for use");

        state = SM_FIRST_TIME_USE;
        break;

    case 1:
        LOG_INF("One bonded device found, CSIP not implemented yet - continuing with single device");
        LOG_INF("HARC HI Remote will now:");
        LOG_INF(" - Connect to the bonded device");
        LOG_INF(" - Discover services and characteristics");
        LOG_INF(" - Ready for (currently) single device use");

        return STRATEGY_ONE_BONDED_DEVICE; // One bonded device
    case 2:
        LOG_INF("Two bonded devices found, connecting to both and verifying set membership");
        return STRATEGY_TWO_BONDED_DEVICES; // Two bonded devices
    default:
        LOG_ERR("Illegal number of bonded devices (%d) found", collection.count);
        return STRATEGY_ILLEGAL_STATE; // Multiple bonded devices
    }

    return 0;
}

// static void execute_strategy(uint8_t strategy)
// {
//     switch (strategy)
//     {
//     case STRATEGY_FIRST_TIME_PAIRING:
//         ble_manager_start_scan_for_HIs();
//         state = SM_FIRST_TIME_USE;
//         break;
//     case STRATEGY_ONE_BONDED_DEVICE:
//         ble_manager_connect_to_bonded_device(NULL);
//         break;
//     case STRATEGY_TWO_BONDED_DEVICES:
//         LOG_WRN("Multiple bonded devices found - currently not supported, defaulting to first device");
//         struct bond_collection *collection = (struct bond_collection *)k_calloc(1, sizeof(struct bond_collection));
//         devices_manager_get_bonded_devices_collection(collection);
//         ble_manager_connect_to_bonded_device(&collection->devices[0].addr);
//         ble_manager_connect_to_bonded_device(&collection->devices[1].addr);
//         break;
//     case STRATEGY_ILLEGAL_STATE:
//         LOG_ERR("Illegal state detected - cannot proceed with connection management");
//         break;
//     default:
//         LOG_ERR("Unknown strategy (%d) - cannot proceed with connection management", strategy);
//         break;
//     }
// }

K_THREAD_DEFINE(app_controller_thread_id, 2048, app_controller_thread, NULL, NULL, NULL, 5, 0, 0);

int8_t app_controller_notify_system_ready()
{
    LOG_DBG("Notifying system ready");
    struct app_event evt = {
        .type = EVENT_SYSTEM_READY,
        .device_id = 0};
    return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_ble_cmd_complete(uint8_t device_id, enum ble_cmd_type type, int8_t err_code)
{
    LOG_DBG("Notifying BLE command complete: device_id=%d, type=%d, err_code=%d", device_id, type, err_code);
    struct app_event evt = {
        .type = EVENT_BLE_CMD_COMPLETE,
        .device_id = device_id,
        .ble_cmd_type = type,
        .error_code = err_code};
    return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_device_connected(uint8_t device_id)
{
    LOG_DBG("Notifying device connected: device_id=%d", device_id);
    struct app_event evt = {
        .type = EVENT_DEVICE_CONNECTED,
        .device_id = device_id};
    return k_msgq_put(&app_event_queue, &evt, K_NO_WAIT);
}

int8_t app_controller_notify_device_ready(uint8_t device_id)
{
    LOG_DBG("Notifying device ready: device_id=%d", device_id);
    struct app_event evt = {
        .type = EVENT_DEVICE_READY,
        .device_id = device_id};
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