#ifndef HAS_CONTROLLER_H
#define HAS_CONTROLLER_H

#include "ble_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/has.h>
#include <zephyr/logging/log.h>

extern bool presets_loaded;

/**
 * @brief Initialize HAS controller
 * 
 * @return 0 on success, negative error code on failure
 */
int has_controller_init(void);

/**
 * @brief Discover HAS on connected device (called from command queue)
 * 
 * @return 0 on success, negative error code on failure
 */
int has_cmd_discover(uint8_t device_id);

/**
 * @brief Read all presets from the hearing aid
 * 
 * @return 0 on success, negative error code on failure
 */
int has_cmd_read_presets(uint8_t device_id);

/**
 * @brief Set active preset by index
 * 
 * @param index Preset index to activate
 * @return 0 on success, negative error code on failure
 */
int has_cmd_set_active_preset(uint8_t device_id, uint8_t index);

/**
 * @brief Activate next preset
 * 
 * @return 0 on success, negative error code on failure
 */
int has_cmd_next_preset(uint8_t device_id);

/**
 * @brief Activate previous preset
 * 
 * @return 0 on success, negative error code on failure
 */
int has_cmd_prev_preset(uint8_t device_id);

/**
 * @brief Get information about a specific preset
 * 
 * @param index Preset index
 * @param preset_out Output buffer for preset info
 * @return 0 on success, negative error code on failure
 */
int has_get_preset_info(uint8_t device_id,uint8_t index, struct has_preset_info *preset_out);

/**
 * @brief Get the currently active preset index
 * 
 * @return Active preset index, or -1 if none active
 */
int has_get_active_preset(uint8_t device_id);

/**
 * @brief Reset HAS controller state
 */
void has_controller_reset(uint8_t device_id);

#endif /* HAS_CONTROLLER_H */