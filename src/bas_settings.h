/**
 * @file bas_settings.h
 * @brief BAS handle caching in NVS settings
 */

#ifndef BAS_SETTINGS_H_
#define BAS_SETTINGS_H_

#include <zephyr/bluetooth/bluetooth.h>
#include <stdint.h>

/**
 * @brief BAS handles structure for caching
 */
struct bt_bas_handles {
	uint16_t service_handle;
	uint16_t service_handle_end;
	uint16_t battery_level_handle;
};

/**
 * @brief Store BAS handles to NVS
 *
 * @param addr Bluetooth address of the device
 * @param handles BAS handles structure to store
 * @return 0 on success, negative errno on failure
 */
int bas_settings_store_handles(const bt_addr_le_t *addr,
                               const struct bt_bas_handles *handles);

/**
 * @brief Load BAS handles from NVS
 *
 * @param addr Bluetooth address of the device
 * @param handles Buffer to store loaded handles
 * @return 0 on success, -ENOENT if not found, negative errno on failure
 */
int bas_settings_load_handles(const bt_addr_le_t *addr,
                              struct bt_bas_handles *handles);

/**
 * @brief Clear BAS handles from NVS
 *
 * @param addr Bluetooth address of the device
 * @return 0 on success, negative errno on failure
 */
int bas_settings_clear_handles(const bt_addr_le_t *addr);

#endif /* BAS_SETTINGS_H_ */
