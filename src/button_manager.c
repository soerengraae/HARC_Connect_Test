#include "button_manager.h"
#include "app_controller.h"

LOG_MODULE_REGISTER(button_manager, LOG_LEVEL_DBG);

/* Button definitions from device tree */
static struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_dt_spec button2 = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);
static struct gpio_dt_spec button3 = GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios);
static struct gpio_dt_spec button4 = GPIO_DT_SPEC_GET(DT_ALIAS(sw3), gpios);

struct gpio_callback button1_cb_data;
struct gpio_callback button2_cb_data;
struct gpio_callback button3_cb_data;
struct gpio_callback button4_cb_data;

struct gpio_dt_spec* get_button_by_id(uint8_t button_id);

bool button_manager_buttons_ready = false;

void button_manager_reset_buttons(void) {
    button_manager_buttons_ready = false;
}

int button_manager_init_buttons(void)
{
    int ret;

    /** Check if devices are ready */
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

    if (!gpio_is_ready_dt(&button4)) {
        LOG_ERR("Button 4 device not ready");
        return -ENODEV;
    }

    /** Configure them as input */
    ret = gpio_pin_configure_dt(&button1, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 1 (err %d)", ret);
        return ret;
    }
    LOG_DBG("Configured button 1 on port %s pin %d", button1.port->name, button1.pin);

    ret = gpio_pin_configure_dt(&button2, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 2 (err %d)", ret);
        return ret;
    }
    LOG_DBG("Configured button 2 on port %s pin %d", button2.port->name, button2.pin);

    ret = gpio_pin_configure_dt(&button3, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 3 (err %d)", ret);
        return ret;
    }
    LOG_DBG("Configured button 3 on port %s pin %d", button3.port->name, button3.pin);

    ret = gpio_pin_configure_dt(&button4, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Failed to configure button 4 (err %d)", ret);
        return ret;
    }
    LOG_DBG("Configured button 4 on port %s pin %d", button4.port->name, button4.pin);

    /** Set interrupt modes */
    for (ssize_t i = 1; i <= 4; i++) {
        button_manager_set_button_interrupt_mode(i, GPIO_INT_EDGE_TO_ACTIVE);
        LOG_DBG("Set button %d interrupt to EDGE_TO_ACTIVE", i);
    }

    /** Add callbacks */
    gpio_init_callback(&button1_cb_data, button1_pressed, BIT(button1.pin));
    ret = gpio_add_callback(button1.port, &button1_cb_data);
    if (ret != 0) {
        LOG_ERR("Failed to add callback for button 1 (err %d)", ret);
        return ret;
    }
    LOG_DBG("Added callback for button 1");

    gpio_init_callback(&button2_cb_data, button2_pressed, BIT(button2.pin));
    ret = gpio_add_callback(button2.port, &button2_cb_data);
    if (ret != 0) {
        LOG_ERR("Failed to add callback for button 2 (err %d)", ret);
        return ret;
    }
    LOG_DBG("Added callback for button 2");

    gpio_init_callback(&button3_cb_data, button3_pressed, BIT(button3.pin));
    ret = gpio_add_callback(button3.port, &button3_cb_data);
    if (ret != 0) {
        LOG_ERR("Failed to add callback for button 3 (err %d)", ret);
        return ret;
    }
    LOG_DBG("Added callback for button 3");

    gpio_init_callback(&button4_cb_data, button4_pressed, BIT(button4.pin));
    ret = gpio_add_callback(button4.port, &button4_cb_data);
    if (ret != 0) {
        LOG_ERR("Failed to add callback for button 4 (err %d)", ret);
        return ret;
    }
    LOG_DBG("Added callback for button 4");

    button_manager_buttons_ready = true;

    return 0;
}

void button_manager_set_button_interrupt_mode(uint8_t button_id, gpio_flags_t mode)
{
    const struct gpio_dt_spec *button = get_button_by_id(button_id);
    if (button == NULL) {
        LOG_ERR("Invalid button ID: %d", button_id);
        return;
    }

    int ret = gpio_pin_interrupt_configure_dt(button, mode);
    if (ret != 0) {
        LOG_ERR("Failed to configure button interrupt (err %d)", ret);
    }
}

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
    LOG_WRN("Button 3 pressed - Pairing!");
    app_controller_notify_pair_button_pressed();
}

void button4_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    LOG_INF("Button 4 pressed - Next Preset");
    app_controller_notify_preset_button_pressed();
}

struct gpio_dt_spec* get_button_by_id(uint8_t button_id)
{
    switch (button_id) {
        case 1:
            return &button1;
        case 2:
            return &button2;
        case 3:
            return &button3;
        case 4:
            return &button4;
        default:
            return NULL;
    }
}