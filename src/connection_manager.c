#include "connection_manager.h"
#include "ble_manager.h"
#include "devices_manager.h"

LOG_MODULE_REGISTER(connection_manager, LOG_LEVEL_DBG);

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
        LOG_INF(" - Discover services and characteristics");
        LOG_INF(" - Ready for (currently) single device use");
        ble_manager_scan_for_HIs();

        return 0; // No bonded devices
    case 1:
        LOG_INF("One bonded device found, CSIP not implemented yet - continuing with single device");
        LOG_INF("HARC HI Remote will now:");
        LOG_INF(" - Connect to the bonded device");
        LOG_INF(" - Discover services and characteristics");
        LOG_INF(" - Ready for (currently) single device use");
        ble_manager_connect_to_bonded_device(NULL);
        return 1; // One bonded device
    case 2:
        LOG_INF("Two bonded devices found, CSIP not implemented yet - continuing with single device (first bond entry)");
        return 2; // Multiple bonded devices
    default:
        LOG_ERR("Illegal number of bonded devices (%d) found", collection.count);
        return 3; // Multiple bonded devices
    }

    return 0;
}

uint8_t connection_manager_init(void)
{
    determine_strategy();
    return 0;
}