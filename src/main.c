#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include "ble_manager.h"
#include "vcp_controller.h"
#include "csip_coordinator.h"
#include "devices_manager.h"
#include "battery_reader.h"
#include "display_manager.h"
#include "app_controller.h"
#include "oled_display.h"
#include "button_handler.h"
#include "has_controller.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Button definitions from device tree */
static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec button2 = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);
static const struct gpio_dt_spec button3 = GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios);
static const struct gpio_dt_spec button4 = GPIO_DT_SPEC_GET(DT_ALIAS(sw3), gpios);
static struct gpio_callback button4_cb_data;
static struct gpio_callback button1_cb_data;
static struct gpio_callback button2_cb_data;
static struct gpio_callback button3_cb_data;

/* Button callbacks */
void button1_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    LOG_INF("Button 1 pressed - Volume Up");
    app_controller_notify_volume_up_button_pressed();
}

void button2_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    LOG_INF("Button 2 pressed - Volume Down");
    app_controller_notify_volume_down_button_pressed();
}

void button3_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    LOG_WRN("Button 3 pressed - CLEARING ALL BONDS!");
    app_controller_notify_clear_bonds_button_pressed();
}

void button4_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    LOG_INF("Button 4 pressed - Next Preset");
    app_controller_notify_preset_button_pressed();
}

static int init_buttons(void)
{
    int ret;

    if (!gpio_is_ready_dt(&button1)) {
        LOG_ERR("Button 1 device not ready");
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&button2)) {
        LOG_ERR("Button 2 device not ready");
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&button3)) {
        LOG_ERR("Button 3 device not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&button1, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 1 (err %d)", ret);
        return ret;
    }

    ret = gpio_pin_configure_dt(&button2, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 2 (err %d)", ret);
        return ret;
    }

    ret = gpio_pin_configure_dt(&button3, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 3 (err %d)", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&button1, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 1 interrupt (err %d)", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&button2, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 2 interrupt (err %d)", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&button3, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 3 interrupt (err %d)", ret);
        return ret;
    }

    if (!gpio_is_ready_dt(&button4)) {
        LOG_ERR("Button 4 device not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&button4, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 4 (err %d)", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&button4, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 4 interrupt (err %d)", ret);
        return ret;
    }

    gpio_init_callback(&button4_cb_data, button4_pressed, BIT(button4.pin));
    gpio_add_callback(button4.port, &button4_cb_data);

    gpio_init_callback(&button1_cb_data, button1_pressed, BIT(button1.pin));
    gpio_add_callback(button1.port, &button1_cb_data);

    gpio_init_callback(&button2_cb_data, button2_pressed, BIT(button2.pin));
    gpio_add_callback(button2.port, &button2_cb_data);

    gpio_init_callback(&button3_cb_data, button3_pressed, BIT(button3.pin));
    gpio_add_callback(button3.port, &button3_cb_data);

    LOG_INF("Buttons initialized: Button 1 = Vol Up, Button 2 = Vol Down, Button 3 = Clear Bonds, Button 4 = Next Preset");
    return 0;
}

int main(void)
{
    int err;

    // /* Initialize work queue for display updates */
    // k_work_init(&display_update_work, display_update_work_handler);

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        err = settings_subsys_init();
        if (err) {
            LOG_ERR("Settings init failed (err %d)", err);
        }

        err = settings_load();
        if (err) {
            LOG_ERR("Settings load failed (err %d)", err);
        }
    }

    err = vcp_controller_init();
	if (err) {
		LOG_ERR("VCP controller init failed (err %d)", err);
		return err;
	}

    err = battery_reader_init();
	if (err)
	{
		LOG_ERR("Battery reader init failed (err %d)", err);
		return err;
	}

	err = csip_coordinator_init();
	if (err)
	{
		LOG_ERR("CSIP coordinator init failed (err %d)", err);
		return err;
	}

    // LOG_INF("Starting display initialization...");
    // err = display_manager_init();
    // if (err) {
    //     LOG_WRN("Display manager init failed (err %d) - continuing without display", err);
    // }
    // LOG_INF("Display initialization completed");
    err = has_controller_init();
    if (err) {
        LOG_ERR("HAS controller init failed (err %d)", err);
        return err;
    }

    // LOG_INF("Starting display initialization...");
    // err = display_manager_init();
    // if (err) {
    //     LOG_WRN("Display manager init failed (err %d) - continuing without display", err);
    // }
    // LOG_INF("Display initialization completed");

    /* Initialize buttons */
    err = init_buttons();
    if (err) {
        LOG_ERR("Button init failed (err %d)", err);
        return err;
    }

    /* Initialize Bluetooth */
    err = bt_enable(bt_ready_cb);
    // /* Initialize OLED display */
    // err = oled_display_init();
    // if (err) {
    //     LOG_ERR("OLED init failed (err %d)", err);
    //     return err;
    // }
    // oled_display_status("INITIALIZING");

    // /* Initialize button handlers */
    // err = button_handler_init();
    // if (err) {
    //     LOG_ERR("Button handler init failed (err %d)", err);
    //     return err;
    // }

    while (1) {
        k_sleep(K_SECONDS(1));

        // Update display every second
        // display_refresh_periodic();
    }

    return 0;
}