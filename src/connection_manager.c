#include "connection_manager.h"
#include "ble_manager.h"
#include "devices_manager.h"

LOG_MODULE_REGISTER(connection_manager, LOG_LEVEL_DBG);

uint8_t strategy;

/**
 * @brief Determine connection strategy based on bonded devices
 * @return Connection strategy enum value
 */
static uint8_t determine_strategy(void)
{
  struct bond_collection collection;
  devices_manager_get_bonded_devices_collection(&collection);

  switch (collection.count) {
    case 0:
        LOG_INF("No bonded devices found");
        LOG_INF("HARC HI Remote will now:");
        LOG_INF(" - Scan for nearby HARC HI devices");
        LOG_INF(" - Connect to the first device found");
        LOG_INF(" - Pair and bond automatically");
        LOG_INF(" - Discover CSIP services and characteristics");
        LOG_INF(" - Store SIRK and rank for automatic reconnection next time");
        LOG_INF(" - Scan for RSI advertisements, resolve RSI, connect if match,\n   discover SIRK, compare SIRK, and bond if match");
        LOG_INF(" - Discover other services and characteristics for both devices");
        LOG_INF(" - Ready for use");

        return STRATEGY_NO_BONDED_DEVICES; // No bonded devices
    case 1:
        LOG_INF("One bonded device found, CSIP not implemented yet - continuing with single device");
        LOG_INF("HARC HI Remote will now:");
        LOG_INF(" - Connect to the bonded device");
        LOG_INF(" - Discover services and characteristics");
        LOG_INF(" - Ready for (currently) single device use");
        
        return STRATEGY_ONE_BONDED_DEVICE; // One bonded device
    case 2:
        LOG_INF("Two bonded devices found, CSIP not implemented yet - continuing with single device (first bond entry)");
        return STRATEGY_TWO_BONDED_DEVICES; // Two bonded devices
    default:
        LOG_ERR("Illegal number of bonded devices (%d) found", collection.count);
        return STRATEGY_ILLEGAL_STATE; // Multiple bonded devices
    }

    return 0;
}

static void execute_strategy(uint8_t strategy)
{
    switch (strategy) {
        case STRATEGY_NO_BONDED_DEVICES:
            ble_manager_scan_for_HIs();
            break;
        case STRATEGY_ONE_BONDED_DEVICE:
            ble_manager_connect_to_bonded_device(NULL);
            break;
        case STRATEGY_TWO_BONDED_DEVICES:
            LOG_WRN("Multiple bonded devices found - currently not supported, defaulting to first device");
            // ble_manager_connect_to_bonded_device(NULL);
            break;
        case STRATEGY_ILLEGAL_STATE:
            LOG_ERR("Illegal state detected - cannot proceed with connection management");
            break;
        default:
            LOG_ERR("Unknown strategy (%d) - cannot proceed with connection management", strategy);
            break;
    }
}

uint8_t connection_manager_init(void)
{
    strategy = determine_strategy();
    execute_strategy(strategy);
    return 0;
}