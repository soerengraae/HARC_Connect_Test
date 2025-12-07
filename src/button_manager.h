#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

extern bool button_manager_buttons_ready;

#define VOLUME_UP_BTN_PIN DT_GPIO_PIN(DT_ALIAS(sw0), gpios)
#define VOLUME_DOWN_BTN_PIN DT_GPIO_PIN(DT_ALIAS(sw1), gpios)
#define PAIR_BTN_PIN DT_GPIO_PIN(DT_ALIAS(sw2), gpios)
#define NEXT_PRESET_BTN_PIN DT_GPIO_PIN(DT_ALIAS(sw3), gpios)

#define VOLUME_UP_BTN_ID 1
#define VOLUME_DOWN_BTN_ID 2
#define PAIR_BTN_ID 3
#define NEXT_PRESET_BTN_ID 4

void button1_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void button2_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void button3_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
void button4_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

int button_manager_init_buttons(void);
void button_manager_reset_buttons(void);
void button_manager_set_button_interrupt_mode(uint8_t button_id, gpio_flags_t mode);

#endif /* BUTTON_MANAGER_H */