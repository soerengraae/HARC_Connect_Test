/**
 * @file has_settings.h
 * @brief HAS handle caching in NVS settings
 */

#ifndef HAS_SETTINGS_H_
#define HAS_SETTINGS_H_

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/audio/has.h>

/**
 * @brief Store HAS handles to NVS
 *
 * @param addr Bluetooth address of the device
 * @param handles HAS handles structure to store
 * @return 0 on success, negative errno on failure
 */
int has_settings_store_handles(const bt_addr_le_t *addr,
                                const struct bt_has_handles *handles);

/**
 * @brief Load HAS handles from NVS
 *
 * @param addr Bluetooth address of the device
 * @param handles Buffer to store loaded handles
 * @return 0 on success, -ENOENT if not found, negative errno on failure
 */
int has_settings_load_handles(const bt_addr_le_t *addr,
                               struct bt_has_handles *handles);

/**
 * @brief Clear HAS handles from NVS
 *
 * @param addr Bluetooth address of the device
 * @return 0 on success, negative errno on failure
 */
int has_settings_clear_handles(const bt_addr_le_t *addr);

#endif /* HAS_SETTINGS_H_ */
