#include "display_manager.h"
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(display_manager, LOG_LEVEL_INF);

static const struct device *display_dev;

static void display_refresh(void)
{
    char buf[32];

    cfb_framebuffer_clear(display_dev, false);


    // Battery Level
    snprintf(buf, sizeof(buf), "Bat: %d%%", battery_level);
    cfb_print(display_dev, buf, 0, 18);

    // Convert volume from 0-255 to 0-100%
    uint8_t volume_percent = (uint8_t)((current_volume * 100) / 255);
    snprintf(buf, sizeof(buf), "Vol: %d%%", volume_percent);
    cfb_print(display_dev, buf, 0, 36);

    cfb_framebuffer_finalize(display_dev);
}

int display_manager_init(void)
{
    int err;

    LOG_INF("Display init: Getting device");
    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device not ready");
        return -ENODEV;
    }

    LOG_INF("Display init: Device ready, initializing framebuffer");
    err = cfb_framebuffer_init(display_dev);
    if (err) {
        LOG_ERR("Failed to init framebuffer (err %d)", err);
        return err;
    }

    LOG_INF("Display init: Clearing framebuffer");
    cfb_framebuffer_clear(display_dev, true);

    LOG_INF("Display init: Setting font");
    err = cfb_framebuffer_set_font(display_dev,0);
    if (err) {
        LOG_ERR("Failed to set font (err %d)", err);
        return err;
    }

    LOG_INF("Display init: Printing text");
    cfb_print(display_dev, "Resound", 0, 0);

    LOG_INF("Display init: Finalizing framebuffer");
    cfb_framebuffer_finalize(display_dev);

    LOG_INF("Display initialized successfully");
    return 0;
}


void display_refresh_periodic(void)
{
    display_refresh();
}