/*
 * Button Handler for nRF52832 DK
 * Header file
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <zephyr/kernel.h>

/* Button callback function type */
typedef void (*button_callback_t)(void);

/* Initialize button handlers */
int button_handler_init(void);

/* Register callbacks for button events */
void button_register_volume_up_callback(button_callback_t callback);
void button_register_volume_down_callback(button_callback_t callback);

#endif /* BUTTON_HANDLER_H */
