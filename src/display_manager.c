#include "display_manager.h"
#include <stdio.h>
#include <string.h>
#include "has_controller.h"

LOG_MODULE_REGISTER(display_manager, LOG_LEVEL_INF);

static const struct device *display_dev;

/**
 * @brief Draw a volume bar indicator
 *
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Width of the bar in pixels
 * @param height Height of the bar in pixels
 * @param volume Current volume (0-255)
 */
static void draw_volume_bar(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t volume)
{
    struct cfb_position start, end;

    // Draw outer border
    start.x = x;
    start.y = y;
    end.x = x + width - 1;
    end.y = y + height - 1;
    cfb_draw_rect(display_dev, &start, &end);

    // Calculate fill width based on volume (0-255)
    uint16_t fill_width = ((width - 4) * volume) / 255;  // -4 for borders and padding

    // Draw filled bar inside the border (if volume > 0)
    if (fill_width > 0) {
        start.x = x + 2;
        start.y = y + 2;
        end.x = x + 2 + fill_width;
        end.y = y + height - 3;

        // Fill by drawing multiple vertical lines
        for (uint16_t i = start.x; i <= end.x && i < x + width - 2; i++) {
            struct cfb_position line_start = {i, start.y};
            struct cfb_position line_end = {i, end.y};
            cfb_draw_line(display_dev, &line_start, &line_end);
        }
    }
}

static void display_refresh(void)
{
    char buf[32];

    cfb_framebuffer_clear(display_dev, false);


    // Battery Level
    snprintf(buf, sizeof(buf), "%d%%", battery_level);
    cfb_print(display_dev, buf, 98, 0);

    // Volume label and bar
    cfb_print(display_dev, "Vol:", 0, 16);
    draw_volume_bar(45, 18, 80, 12, current_volume);


    // Active Preset
    int active_idx = has_get_active_preset();
    if (active_idx >= 0) {
        struct has_preset_info preset_info;
        if (has_get_preset_info(active_idx, &preset_info) == 0) {
            // Truncate preset name if too long
            char short_name[13];  // Max 12 chars + null terminator
            strncpy(short_name, preset_info.name, 12);
            short_name[12] = '\0';
            
            snprintf(buf, sizeof(buf), "%s", short_name);
            cfb_print(display_dev, buf, 0, 38);
        } else {
            snprintf(buf, sizeof(buf), "%d", active_idx);
            cfb_print(display_dev, buf, 0, 38);
        }
    } else if (has_discovered) {
        cfb_print(display_dev, "Preset: None", 0, 54);
    }

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