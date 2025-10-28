#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

#include "ble_manager.h"
#include "vcp_controller.h"
#include "battery_reader.h"
#include "oled_display.h"
#include "button_handler.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Button test tracking */
static uint32_t button_up_count = 0;
static uint32_t button_down_count = 0;

/* VCP state for display */
static uint8_t current_volume = 0;
static bool current_mute = false;

/* Work queue for display updates from interrupt context */
static struct k_work display_update_work;

/* Work handler for display updates (runs in thread context) */
static void display_update_work_handler(struct k_work *work)
{
    char buf[32];

    if (vcp_discovered && vol_ctlr) {
        /* Connected to VCP - show volume display */
        LOG_INF("Updating volume display: %u%%, mute:%d",
                (uint16_t)current_volume * 100 / 255, current_mute);
        oled_display_volume(current_volume, current_mute, true);
    } else {
        /* Not connected - show button test screen */
        LOG_INF("Updating button test display - UP:%u DN:%u",
                button_up_count, button_down_count);

        oled_clear();
        oled_draw_string(5, 0, "BUTTON TEST OK");

        /* Draw a box around the counters */
        oled_draw_rect(0, 14, 127, 28);

        oled_draw_string(4, 18, "UP:");
        snprintk(buf, sizeof(buf), "%u", button_up_count);
        oled_draw_string(35, 18, buf);

        oled_draw_string(4, 30, "DN:");
        snprintk(buf, sizeof(buf), "%u", button_down_count);
        oled_draw_string(35, 30, buf);

        oled_draw_string(10, 52, "PRESS BTNs!");
        oled_display();
    }
}

/* Helper function to show button test feedback */
static void show_button_test_feedback(void)
{
    /* Just submit work to update display in thread context */
    k_work_submit(&display_update_work);
}

/* Public function to update volume display (called from vcp_controller) */
void main_update_volume_display(uint8_t volume, bool mute)
{
    current_volume = volume;
    current_mute = mute;
    k_work_submit(&display_update_work);
}

/* Button callback handlers */
static void on_volume_up_button(void)
{
    button_up_count++;
    LOG_INF("Volume Up button pressed (count: %u)", button_up_count);

    if (vcp_discovered && vol_ctlr) {
        /* Send volume up command - display will update when VCP responds */
        ble_cmd_vcp_volume_up();
    } else {
        /* Show button test feedback when not connected */
        show_button_test_feedback();
    }
}

static void on_volume_down_button(void)
{
    button_down_count++;
    LOG_INF("Volume Down button pressed (count: %u)", button_down_count);

    if (vcp_discovered && vol_ctlr) {
        /* Send volume down command - display will update when VCP responds */
        ble_cmd_vcp_volume_down();
    } else {
        /* Show button test feedback when not connected */
        show_button_test_feedback();
    }
}

int main(void)
{
    int err;

    LOG_INF("HARC Audio Controller Starting...");

    /* Initialize work queue for display updates */
    k_work_init(&display_update_work, display_update_work_handler);

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

    /* Initialize OLED display */
    err = oled_display_init();
    if (err) {
        LOG_ERR("OLED init failed (err %d)", err);
        return err;
    }
    oled_display_status("INITIALIZING");

    /* Initialize button handlers */
    err = button_handler_init();
    if (err) {
        LOG_ERR("Button handler init failed (err %d)", err);
        return err;
    }

    /* Register button callbacks */
    button_register_volume_up_callback(on_volume_up_button);
    button_register_volume_down_callback(on_volume_down_button);

    err = vcp_controller_init();
    if (err) {
        LOG_ERR("VCP controller init failed (err %d)", err);
        oled_display_status("VCP INIT FAIL");
        return err;
    }

    /* Initialize Bluetooth */
    oled_display_status("BT STARTING");
    err = bt_enable(bt_ready_cb);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        oled_display_status("BT INIT FAIL");
        return err;
    }

    /* Show initial button test screen */
    k_sleep(K_MSEC(500));
    show_button_test_feedback();

    LOG_INF("========================================");
    LOG_INF("Button system ready!");
    LOG_INF("Press Button 1 (SW1) for Volume Up");
    LOG_INF("Press Button 2 (SW2) for Volume Down");
    LOG_INF("========================================");

    /* Main loop - interrupt-based buttons working! */
    while (1) {
        k_sleep(K_SECONDS(5));

        /* Periodically read battery level if connected */
        if (battery_discovered) {
            ble_cmd_bas_read_level();
        }

        /* Refresh display if not connected (but don't overwrite too frequently) */
        if (!vcp_discovered) {
            /* Display already updated by button callbacks, just refresh periodically */
            show_button_test_feedback();
        }
    }

    return 0;
}