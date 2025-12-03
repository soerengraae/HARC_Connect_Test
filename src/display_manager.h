#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

/**
 * @brief Initialize the display manager and SSD1306 display
 *
 * @return 0 on success, negative error code on failure
 */
int display_manager_init(void);

/**
 * @brief Update display with current system state
 *
 * This function should be called whenever hearing aid state changes
 */
void display_manager_update(void);

/**
 * @brief Update device connection state on display
 *
 * @param device_id Device ID (0 or 1)
 * @param state Connection state string
 */
void display_manager_update_connection_state(uint8_t device_id, const char *state);

/**
 * @brief Update volume level on display
 *
 * @param device_id Device ID (0 or 1)
 * @param volume Volume level (0-255)
 * @param mute Mute state
 */
void display_manager_update_volume(uint8_t device_id, uint8_t volume, uint8_t mute);

/**
 * @brief Update battery level on display
 *
 * @param device_id Device ID (0 or 1)
 * @param battery_level Battery level (0-100%)
 */
void display_manager_update_battery(uint8_t device_id, uint8_t battery_level);

/**
 * @brief Update active preset on display
 *
 * @param device_id Device ID (0 or 1)
 * @param preset_index Active preset index
 * @param preset_name Active preset name
 */
void display_manager_update_preset(uint8_t device_id, uint8_t preset_index, const char *preset_name);

/**
 * @brief Clear the display
 */
void display_manager_clear(void);

/**
 * @brief Show a status message on the display
 *
 * @param message Status message to display
 */
void display_manager_show_status(const char *message);

/**
 * @brief Put display into sleep mode (low power ~5 µA)
 *
 * Turns off the SSD1306 display using display blanking to reduce
 * power consumption to approximately 5 µA. Frame buffer content
 * is retained in memory.
 *
 * @return 0 on success, negative error code on failure
 */
int display_manager_sleep(void);

/**
 * @brief Wake display from sleep mode
 *
 * Restores the SSD1306 display from sleep mode and restores
 * the frame buffer content to the display.
 *
 * @return 0 on success, negative error code on failure
 */
int display_manager_wake(void);

/**
 * @brief Check if display is currently in sleep mode
 *
 * @return true if display is sleeping, false otherwise
 */
bool display_manager_is_sleeping(void);

#endif /* DISPLAY_MANAGER_H */
