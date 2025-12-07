/**
 * @file vcp_settings.h
 * @brief VCP handle caching in NVS settings
 */

#ifndef VCP_SETTINGS_H_
#define VCP_SETTINGS_H_

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/audio/vcp.h>

/**
 * @brief Store VCP handles to NVS
 *
 * @param addr Bluetooth address of the device
 * @param handles VCP handles structure to store
 * @return 0 on success, negative errno on failure
 */
int vcp_settings_store_handles(const bt_addr_le_t *addr,
                                const struct bt_vcp_vol_ctlr_handles *handles);

/**
 * @brief Load VCP handles from NVS
 *
 * @param addr Bluetooth address of the device
 * @param handles Buffer to store loaded handles
 * @return 0 on success, -ENOENT if not found, negative errno on failure
 */
int vcp_settings_load_handles(const bt_addr_le_t *addr,
                               struct bt_vcp_vol_ctlr_handles *handles);

/**
 * @brief Clear VCP handles from NVS
 *
 * @param addr Bluetooth address of the device
 * @return 0 on success, negative errno on failure
 */
int vcp_settings_clear_handles(const bt_addr_le_t *addr);

#endif /* VCP_SETTINGS_H_ */
