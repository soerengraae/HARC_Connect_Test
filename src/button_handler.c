/*
 * Button Handler for nRF52832 DK
 * Implementation file
 */

#include "button_handler.h"
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(button_handler, LOG_LEVEL_DBG);

/* Button definitions from devicetree */
static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec button2 = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);

/* GPIO callback structures */
static struct gpio_callback button1_cb_data;
static struct gpio_callback button2_cb_data;

/* User-registered callbacks */
static button_callback_t volume_up_callback = NULL;
static button_callback_t volume_down_callback = NULL;

/* Button press handlers */
static void button1_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    LOG_DBG("Button 1 interrupt (Volume Up)");
    if (volume_up_callback != NULL) {
        volume_up_callback();
    }
}

static void button2_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    LOG_DBG("Button 2 interrupt (Volume Down)");
    if (volume_down_callback != NULL) {
        volume_down_callback();
    }
}

/* Initialize button handlers */
int button_handler_init(void)
{
    int ret;

    LOG_INF("Initializing buttons on pins %d (UP) and %d (DOWN)",
            button1.pin, button2.pin);

    /* Check if button devices are ready */
    if (!device_is_ready(button1.port)) {
        LOG_ERR("Button 1 device not ready");
        return -ENODEV;
    }
    if (!device_is_ready(button2.port)) {
        LOG_ERR("Button 2 device not ready");
        return -ENODEV;
    }

    /* Configure buttons as input with pull-up (active-low) */
    ret = gpio_pin_configure_dt(&button1, GPIO_INPUT | GPIO_PULL_UP);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 1 (%d)", ret);
        return ret;
    }

    ret = gpio_pin_configure_dt(&button2, GPIO_INPUT | GPIO_PULL_UP);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 2 (%d)", ret);
        return ret;
    }

    /* Register callbacks */
    gpio_init_callback(&button1_cb_data, button1_pressed, BIT(button1.pin));
    ret = gpio_add_callback(button1.port, &button1_cb_data);
    if (ret != 0) {
        LOG_ERR("Failed to add button 1 callback (%d)", ret);
        return ret;
    }

    gpio_init_callback(&button2_cb_data, button2_pressed, BIT(button2.pin));
    ret = gpio_add_callback(button2.port, &button2_cb_data);
    if (ret != 0) {
        LOG_ERR("Failed to add button 2 callback (%d)", ret);
        return ret;
    }

    /* Configure interrupts */
    ret = gpio_pin_interrupt_configure_dt(&button1, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 1 interrupt (%d)", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&button2, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 2 interrupt (%d)", ret);
        return ret;
    }

    LOG_INF("Button handlers initialized successfully");
    return 0;
}

/* Register volume up callback */
void button_register_volume_up_callback(button_callback_t callback)
{
    volume_up_callback = callback;
}

/* Register volume down callback */
void button_register_volume_down_callback(button_callback_t callback)
{
    volume_down_callback = callback;
}
