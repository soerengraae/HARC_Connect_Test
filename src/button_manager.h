#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

extern bool button_manager_buttons_ready;

void button1_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void button2_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void button3_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void button4_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

int button_manager_init_buttons(void);
void button_manager_reset_buttons(void);
void button_manager_set_button_interrupt_mode(uint8_t button_id, gpio_flags_t mode);

#endif /* BUTTON_MANAGER_H */