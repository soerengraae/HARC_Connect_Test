#include "display_manager.h"
#include "devices_manager.h"
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(display_manager, LOG_LEVEL_INF);

/* Display device */
static const struct device *display_dev;

/* Display dimensions */
static uint16_t display_width;
static uint16_t display_height;
static uint16_t display_ppt; /* Pixels per tile (usually 8) */

/* Display state for both hearing aids */
struct display_state {
    char connection_state[16];
    uint8_t volume;
    bool mute;
    uint8_t battery_level;
    uint8_t active_preset;
    char preset_name[32];
    bool has_data;
};

static struct display_state device_display_state[2] = {0};
static struct k_mutex display_mutex;
static bool display_initialized = false;
static bool display_sleeping = false;

/* Initialize the display */
int display_manager_init(void)
{
    k_mutex_init(&display_mutex);

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device not ready");
        return -ENODEV;
    }

    /* Initialize character framebuffer */
    int err = cfb_framebuffer_init(display_dev);
    if (err) {
        LOG_ERR("Character framebuffer init failed (err %d)", err);
        return err;
    }

    /* Clear the display */
    err = cfb_framebuffer_clear(display_dev, true);
    if (err) {
        LOG_ERR("Failed to clear framebuffer (err %d)", err);
        return err;
    }

    /* Set font - try to get the smallest available font */
    err = cfb_framebuffer_set_font(display_dev, 0);
    if (err) {
        LOG_ERR("Failed to set font (err %d)", err);
        return err;
    }

    /* Get display properties for dynamic layout */
    display_width = cfb_get_display_parameter(display_dev, CFB_DISPLAY_WIDTH);
    display_height = cfb_get_display_parameter(display_dev, CFB_DISPLAY_HEIGHT);
    display_ppt = cfb_get_display_parameter(display_dev, CFB_DISPLAY_PPT);

    LOG_INF("Display initialized: %ux%u px", display_width, display_height);

    /* Initialize display state */
    for (int i = 0; i < 2; i++) {
        strncpy(device_display_state[i].connection_state, "DISC", sizeof(device_display_state[i].connection_state));
        device_display_state[i].volume = 0;
        device_display_state[i].mute = false;
        device_display_state[i].battery_level = 0;
        device_display_state[i].has_data = false;
    }

    display_initialized = true;

    /* Show initial splash screen */
    display_manager_show_status("Resound");

    return 0;
}

void display_manager_clear(void)
{
    if (!display_initialized) {
        return;
    }

    k_mutex_lock(&display_mutex, K_FOREVER);
    cfb_framebuffer_clear(display_dev, true);
    cfb_framebuffer_finalize(display_dev);
    k_mutex_unlock(&display_mutex);
}

void display_manager_show_status(const char *message)
{
    if (!display_initialized || display_sleeping) {
        return;
    }

    k_mutex_lock(&display_mutex, K_FOREVER);
    cfb_framebuffer_clear(display_dev, false);

    /* Calculate centered position (assuming ~8px per character) */
    uint8_t len = strlen(message);
    uint16_t x_pos = (display_width > (len * 8)) ? (display_width - (len * 8)) / 2 : 0;
    uint16_t y_pos = display_height / 2 - 4;

    cfb_print(display_dev, message, x_pos, y_pos);
    cfb_framebuffer_finalize(display_dev);
    k_mutex_unlock(&display_mutex);
}

/* Helper function to trigger display update */
static void trigger_update(void)
{
    display_manager_update();
}

void display_manager_update_connection_state(uint8_t device_id, const char *state)
{
    if (device_id > 1 || !display_initialized) {
        return;
    }

    k_mutex_lock(&display_mutex, K_FOREVER);
    strncpy(device_display_state[device_id].connection_state, state,
            sizeof(device_display_state[device_id].connection_state) - 1);
    device_display_state[device_id].connection_state[sizeof(device_display_state[device_id].connection_state) - 1] = '\0';
    device_display_state[device_id].has_data = true;
    k_mutex_unlock(&display_mutex);

    trigger_update();
}

void display_manager_update_volume(uint8_t device_id, uint8_t volume, uint8_t mute)
{
    if (device_id > 1 || !display_initialized) {
        return;
    }

    k_mutex_lock(&display_mutex, K_FOREVER);
    device_display_state[device_id].volume = volume;
    device_display_state[device_id].mute = mute;
    device_display_state[device_id].has_data = true;
    k_mutex_unlock(&display_mutex);

    trigger_update();
}

void display_manager_update_battery(uint8_t device_id, uint8_t battery_level)
{
    if (device_id > 1 || !display_initialized) {
        return;
    }

    k_mutex_lock(&display_mutex, K_FOREVER);
    device_display_state[device_id].battery_level = battery_level;
    device_display_state[device_id].has_data = true;
    k_mutex_unlock(&display_mutex);

    trigger_update();
}

void display_manager_update_preset(uint8_t device_id, uint8_t preset_index, const char *preset_name)
{
    if (device_id > 1 || !display_initialized) {
        return;
    }

    k_mutex_lock(&display_mutex, K_FOREVER);
    device_display_state[device_id].active_preset = preset_index;
    if (preset_name) {
        strncpy(device_display_state[device_id].preset_name, preset_name,
                sizeof(device_display_state[device_id].preset_name) - 1);
        device_display_state[device_id].preset_name[sizeof(device_display_state[device_id].preset_name) - 1] = '\0';
    } else {
        snprintf(device_display_state[device_id].preset_name,
                 sizeof(device_display_state[device_id].preset_name),
                 "Preset %u", preset_index);
    }
    device_display_state[device_id].has_data = true;
    k_mutex_unlock(&display_mutex);

    trigger_update();
}

/* Icon drawing functions - 32x32 pixel icons */
static void draw_icon_home(uint16_t x, uint16_t y)
{
    struct cfb_position start, end;

    /* Draw house roof (triangle) */
    for (int i = 0; i < 16; i++) {
        start.x = x + 16 - i;
        start.y = y + i;
        end.x = x + 16 + i;
        end.y = y + i;
        cfb_draw_line(display_dev, &start, &end);
    }

    /* Draw house base (rectangle) */
    start.x = x + 4;
    start.y = y + 16;
    end.x = x + 28;
    end.y = y + 32;
    cfb_draw_rect(display_dev, &start, &end);

    /* Draw door */
    start.x = x + 12;
    start.y = y + 22;
    end.x = x + 20;
    end.y = y + 32;
    cfb_draw_rect(display_dev, &start, &end);
}

static void draw_icon_music(uint16_t x, uint16_t y)
{
    struct cfb_position start, end;

    /* Draw musical note stems */
    start.x = x + 12;
    start.y = y + 4;
    end.x = x + 12;
    end.y = y + 24;
    cfb_draw_line(display_dev, &start, &end);

    start.x = x + 20;
    start.y = y + 8;
    end.x = x + 20;
    end.y = y + 24;
    cfb_draw_line(display_dev, &start, &end);

    /* Draw connecting line at top */
    start.x = x + 12;
    start.y = y + 4;
    end.x = x + 20;
    end.y = y + 8;
    cfb_draw_line(display_dev, &start, &end);

    /* Draw note heads (filled circles - approximate with rectangles) */
    start.x = x + 8;
    start.y = y + 22;
    end.x = x + 14;
    end.y = y + 28;
    cfb_draw_rect(display_dev, &start, &end);

    start.x = x + 16;
    start.y = y + 22;
    end.x = x + 22;
    end.y = y + 28;
    cfb_draw_rect(display_dev, &start, &end);
}

static void draw_icon_restaurant(uint16_t x, uint16_t y)
{
    struct cfb_position start, end;

    /* Draw fork on left */
    for (int i = 0; i < 3; i++) {
        start.x = x + 4 + i * 4;
        start.y = y + 4;
        end.x = x + 4 + i * 4;
        end.y = y + 12;
        cfb_draw_line(display_dev, &start, &end);
    }
    /* Fork handle */
    start.x = x + 8;
    start.y = y + 12;
    end.x = x + 8;
    end.y = y + 28;
    cfb_draw_line(display_dev, &start, &end);

    /* Draw knife on right */
    start.x = x + 20;
    start.y = y + 4;
    end.x = x + 20;
    end.y = y + 28;
    cfb_draw_line(display_dev, &start, &end);

    /* Knife blade */
    start.x = x + 18;
    start.y = y + 4;
    end.x = x + 22;
    end.y = y + 12;
    cfb_draw_line(display_dev, &start, &end);
}

static void draw_icon_outdoor(uint16_t x, uint16_t y)
{
    struct cfb_position start, end;

    /* Draw pine tree with three layered triangular sections */

    /* Top triangle section (smallest) */
    for (int i = 0; i < 6; i++) {
        start.x = x + 16 - i;
        start.y = y + 2 + i;
        end.x = x + 16 + i;
        end.y = y + 2 + i;
        cfb_draw_line(display_dev, &start, &end);
    }

    /* Middle triangle section */
    for (int i = 0; i < 8; i++) {
        start.x = x + 16 - i;
        start.y = y + 8 + i;
        end.x = x + 16 + i;
        end.y = y + 8 + i;
        cfb_draw_line(display_dev, &start, &end);
    }

    /* Bottom triangle section (largest) */
    for (int i = 0; i < 10; i++) {
        start.x = x + 16 - i;
        start.y = y + 14 + i;
        end.x = x + 16 + i;
        end.y = y + 14 + i;
        cfb_draw_line(display_dev, &start, &end);
    }

    /* Draw trunk */
    start.x = x + 13;
    start.y = y + 24;
    end.x = x + 19;
    end.y = y + 30;
    cfb_draw_rect(display_dev, &start, &end);
}

static void draw_icon_tv(uint16_t x, uint16_t y)
{
    struct cfb_position start, end;

    /* Draw TV screen */
    start.x = x + 4;
    start.y = y + 8;
    end.x = x + 28;
    end.y = y + 24;
    cfb_draw_rect(display_dev, &start, &end);

    /* Draw antenna left */
    start.x = x + 10;
    start.y = y + 8;
    end.x = x + 6;
    end.y = y + 2;
    cfb_draw_line(display_dev, &start, &end);

    /* Draw antenna right */
    start.x = x + 22;
    start.y = y + 8;
    end.x = x + 26;
    end.y = y + 2;
    cfb_draw_line(display_dev, &start, &end);

    /* Draw stand */
    start.x = x + 14;
    start.y = y + 24;
    end.x = x + 18;
    end.y = y + 30;
    cfb_draw_line(display_dev, &start, &end);
}

static void draw_icon_phone(uint16_t x, uint16_t y)
{
    struct cfb_position start, end;

    /* Draw phone body */
    start.x = x + 8;
    start.y = y + 4;
    end.x = x + 24;
    end.y = y + 28;
    cfb_draw_rect(display_dev, &start, &end);

    /* Draw speaker at top */
    start.x = x + 12;
    start.y = y + 8;
    end.x = x + 20;
    end.y = y + 10;
    cfb_draw_rect(display_dev, &start, &end);

    /* Draw microphone at bottom */
    start.x = x + 12;
    start.y = y + 22;
    end.x = x + 20;
    end.y = y + 24;
    cfb_draw_rect(display_dev, &start, &end);
}

static void draw_icon_default(uint16_t x, uint16_t y)
{
    struct cfb_position start, end;

    /* Draw a generic "preset" icon - a circle with a dot */
    /* Draw circle outline */
    for (int i = 0; i < 24; i++) {
        int offset = i < 12 ? i : 23 - i;
        start.x = x + 4 + offset;
        start.y = y + 4 + i;
        cfb_draw_point(display_dev, &start);

        start.x = x + 28 - offset;
        cfb_draw_point(display_dev, &start);
    }

    /* Draw center dot */
    start.x = x + 14;
    start.y = y + 14;
    end.x = x + 18;
    end.y = y + 18;
    cfb_draw_rect(display_dev, &start, &end);
}

/* Determine and draw appropriate icon based on preset name */
static void draw_preset_icon(uint16_t x, uint16_t y, const char *preset_name)
{
    if (!preset_name) {
        draw_icon_default(x, y);
        return;
    }

    if (strstr(preset_name, "Home") || strstr(preset_name, "home") ||
        strstr(preset_name, "Indoor") || strstr(preset_name, "indoor")) {
        draw_icon_home(x, y);
    } else if (strstr(preset_name, "Music") || strstr(preset_name, "music")) {
        draw_icon_music(x, y);
    } else if (strstr(preset_name, "Restaurant") || strstr(preset_name, "restaurant") ||
               strstr(preset_name, "Party") || strstr(preset_name, "party")) {
        draw_icon_restaurant(x, y);
    } else if (strstr(preset_name, "Outdoor") || strstr(preset_name, "outdoor")) {
        draw_icon_outdoor(x, y);
    } else if (strstr(preset_name, "TV") || strstr(preset_name, "tv") ||
               strstr(preset_name, "Television") || strstr(preset_name, "television")) {
        draw_icon_tv(x, y);
    } else if (strstr(preset_name, "Phone") || strstr(preset_name, "phone") ||
               strstr(preset_name, "Call") || strstr(preset_name, "call")) {
        draw_icon_phone(x, y);
    } else {
        draw_icon_default(x, y);
    }
}

/* Draw vertical volume bar that fills from bottom to top */
static void draw_volume_bar(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                             uint8_t volume, bool mute)
{
    struct cfb_position start, end;

    /* Draw outline */
    start.x = x;
    start.y = y;
    end.x = x + width;
    end.y = y + height;
    cfb_draw_rect(display_dev, &start, &end);

    if (mute) {
        return;
    }

    /* Calculate and draw fill level (0-255 volume range) */
    uint16_t inner_height = height - 4;
    uint16_t filled_height = (inner_height * volume) / 255;

    if (filled_height > 0) {
        start.x = x + 2;
        start.y = y + height - 2 - filled_height;
        end.x = x + width - 2;
        end.y = y + height - 2;

        for (int i = start.y; i < end.y; i++) {
            struct cfb_position line_start = {start.x, i};
            struct cfb_position line_end = {end.x, i};
            cfb_draw_line(display_dev, &line_start, &line_end);
        }
    }
}

void display_manager_update(void)
{
    if (!display_initialized || display_sleeping) {
        return;
    }

    k_mutex_lock(&display_mutex, K_FOREVER);

    char line_buf[32];

    cfb_framebuffer_clear(display_dev, false);

    /* Display battery levels at top */
    snprintf(line_buf, sizeof(line_buf), "L:%u%%", device_display_state[0].battery_level);
    cfb_print(display_dev, line_buf, 0, 0);

    snprintf(line_buf, sizeof(line_buf), "R:%u%%", device_display_state[1].battery_level);
    cfb_print(display_dev, line_buf, display_width - 64, 0);

    /* Display preset icon (32x32 pixels) centered at bottom */
    const char *preset_name_to_show = NULL;
    bool show_preset = false;

    if (device_display_state[0].has_data && device_display_state[0].active_preset > 0) {
        preset_name_to_show = device_display_state[0].preset_name;
        show_preset = true;
    } else if (device_display_state[1].has_data && device_display_state[1].active_preset > 0) {
        preset_name_to_show = device_display_state[1].preset_name;
        show_preset = true;
    }

    if (show_preset && preset_name_to_show) {
        uint16_t icon_width = 32;
        uint16_t icon_height = 32;
        uint16_t x_pos = (display_width - icon_width) / 2;
        uint16_t y_pos = display_height - 4 - icon_height;

        draw_preset_icon(x_pos, y_pos, preset_name_to_show);
    }

    /* Display volume bars at bottom corners */
    uint16_t bar_width = 16;
    uint16_t bar_height = 40;
    uint16_t bar_y = display_height - bar_height - 4;
    uint16_t left_bar_x = 4;
    uint16_t right_bar_x = display_width - bar_width - 4;

    if (device_display_state[0].has_data) {
        draw_volume_bar(left_bar_x, bar_y, bar_width, bar_height,
                        device_display_state[0].volume, device_display_state[0].mute);
    }

    if (device_display_state[1].has_data) {
        draw_volume_bar(right_bar_x, bar_y, bar_width, bar_height,
                        device_display_state[1].volume, device_display_state[1].mute);
    }

    cfb_framebuffer_finalize(display_dev);
    k_mutex_unlock(&display_mutex);
}

int display_manager_sleep(void)
{
    int err;

    if (!display_initialized) {
        LOG_WRN("Cannot sleep display - not initialized");
        return -ENODEV;
    }

    if (display_sleeping) {
        LOG_DBG("Display already sleeping");
        return 0;
    }

    k_mutex_lock(&display_mutex, K_FOREVER);

    /* Use Zephyr's display blanking API to turn off display */
    err = display_blanking_on(display_dev);
    if (err) {
        LOG_ERR("Failed to enable display blanking (err %d)", err);
        k_mutex_unlock(&display_mutex);
        return err;
    }

    display_sleeping = true;
    LOG_INF("Display entered sleep mode");

    k_mutex_unlock(&display_mutex);
    return 0;
}

int display_manager_wake(void)
{
    int err;

    if (!display_initialized) {
        LOG_WRN("Cannot wake display - not initialized");
        return -ENODEV;
    }

    if (!display_sleeping) {
        LOG_DBG("Display already awake");
        return 0;
    }

    k_mutex_lock(&display_mutex, K_FOREVER);

    /* Use Zephyr's display blanking API to turn on display */
    err = display_blanking_off(display_dev);
    if (err) {
        LOG_ERR("Failed to disable display blanking (err %d)", err);
        k_mutex_unlock(&display_mutex);
        return err;
    }

    display_sleeping = false;
    LOG_INF("Display woken from sleep mode");

    k_mutex_unlock(&display_mutex);
    return 0;
}

bool display_manager_is_sleeping(void)
{
    return display_sleeping;
}