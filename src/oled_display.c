/*
 * OLED Display Driver for JMD0.96C (SSD1306)
 * Implementation file
 */

#include "oled_display.h"
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(oled_display, LOG_LEVEL_DBG);

/* OLED Configuration */
#define OLED_I2C_ADDR 0x3C

/* SSD1306 Commands */
#define SSD1306_SETCONTRAST 0x81
#define SSD1306_DISPLAYALLON_RESUME 0xA4
#define SSD1306_NORMALDISPLAY 0xA6
#define SSD1306_INVERTDISPLAY 0xA7
#define SSD1306_DISPLAYOFF 0xAE
#define SSD1306_DISPLAYON 0xAF
#define SSD1306_SETDISPLAYOFFSET 0xD3
#define SSD1306_SETCOMPINS 0xDA
#define SSD1306_SETVCOMDETECT 0xDB
#define SSD1306_SETDISPLAYCLOCKDIV 0xD5
#define SSD1306_SETPRECHARGE 0xD9
#define SSD1306_SETMULTIPLEX 0xA8
#define SSD1306_SETSTARTLINE 0x40
#define SSD1306_MEMORYMODE 0x20
#define SSD1306_COLUMNADDR 0x21
#define SSD1306_PAGEADDR 0x22
#define SSD1306_COMSCANINC 0xC0
#define SSD1306_COMSCANDEC 0xC8
#define SSD1306_SEGREMAP 0xA0
#define SSD1306_CHARGEPUMP 0x8D

/* Display buffer */
static uint8_t oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8];

/* I2C device */
static const struct device *i2c_dev;

/* 5x8 Font - Basic ASCII characters */
static const uint8_t font5x8[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // (space)
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x00, 0x08, 0x14, 0x22, 0x41}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x41, 0x22, 0x14, 0x08, 0x00}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x01, 0x01}, // F
    {0x3E, 0x41, 0x41, 0x51, 0x32}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
};

/* Function to send command to OLED */
static int oled_write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd}; // 0x00 = command mode
    return i2c_write(i2c_dev, buf, sizeof(buf), OLED_I2C_ADDR);
}

/* Function to send data to OLED */
static int oled_write_data(uint8_t *data, size_t len)
{
    uint8_t buf[len + 1];
    buf[0] = 0x40; // 0x40 = data mode
    memcpy(&buf[1], data, len);
    return i2c_write(i2c_dev, buf, len + 1, OLED_I2C_ADDR);
}

/* Initialize OLED display */
int oled_display_init(void)
{
    /* Get I2C device */
    i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C device not ready");
        return -ENODEV;
    }

    LOG_INF("I2C device ready");

    /* Initialization sequence */
    oled_write_cmd(SSD1306_DISPLAYOFF);
    oled_write_cmd(SSD1306_SETDISPLAYCLOCKDIV);
    oled_write_cmd(0x80);
    oled_write_cmd(SSD1306_SETMULTIPLEX);
    oled_write_cmd(0x3F);
    oled_write_cmd(SSD1306_SETDISPLAYOFFSET);
    oled_write_cmd(0x00);
    oled_write_cmd(SSD1306_SETSTARTLINE | 0x00);
    oled_write_cmd(SSD1306_CHARGEPUMP);
    oled_write_cmd(0x14); // Enable charge pump
    oled_write_cmd(SSD1306_MEMORYMODE);
    oled_write_cmd(0x00); // Horizontal addressing mode
    oled_write_cmd(SSD1306_SEGREMAP | 0x01);
    oled_write_cmd(SSD1306_COMSCANDEC);
    oled_write_cmd(SSD1306_SETCOMPINS);
    oled_write_cmd(0x12);
    oled_write_cmd(SSD1306_SETCONTRAST);
    oled_write_cmd(0xCF);
    oled_write_cmd(SSD1306_SETPRECHARGE);
    oled_write_cmd(0xF1);
    oled_write_cmd(SSD1306_SETVCOMDETECT);
    oled_write_cmd(0x40);
    oled_write_cmd(SSD1306_DISPLAYALLON_RESUME);
    oled_write_cmd(SSD1306_NORMALDISPLAY);
    oled_write_cmd(SSD1306_DISPLAYON);

    /* Clear display */
    oled_clear();
    oled_display();

    LOG_INF("OLED initialized");
    return 0;
}

/* Clear display buffer */
void oled_clear(void)
{
    memset(oled_buffer, 0, sizeof(oled_buffer));
}

/* Update display with buffer contents */
void oled_display(void)
{
    oled_write_cmd(SSD1306_COLUMNADDR);
    oled_write_cmd(0);
    oled_write_cmd(OLED_WIDTH - 1);
    oled_write_cmd(SSD1306_PAGEADDR);
    oled_write_cmd(0);
    oled_write_cmd((OLED_HEIGHT / 8) - 1);

    /* Send buffer in chunks */
    for (int i = 0; i < sizeof(oled_buffer); i += 16) {
        int chunk_size = (sizeof(oled_buffer) - i) < 16 ?
                         (sizeof(oled_buffer) - i) : 16;
        oled_write_data(&oled_buffer[i], chunk_size);
    }
}

/* Draw a pixel */
void oled_draw_pixel(int x, int y, uint8_t color)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }

    if (color) {
        oled_buffer[x + (y / 8) * OLED_WIDTH] |= (1 << (y % 8));
    } else {
        oled_buffer[x + (y / 8) * OLED_WIDTH] &= ~(1 << (y % 8));
    }
}

/* Draw a character */
void oled_draw_char(int x, int y, char c)
{
    if (c < 32 || c > 90) {
        c = 32; // Space
    }

    const uint8_t *glyph = font5x8[c - 32];

    for (int i = 0; i < 5; i++) {
        uint8_t line = glyph[i];
        for (int j = 0; j < 8; j++) {
            if (line & 0x01) {
                oled_draw_pixel(x + i, y + j, 1);
            }
            line >>= 1;
        }
    }
}

/* Draw a string */
void oled_draw_string(int x, int y, const char *str)
{
    while (*str) {
        oled_draw_char(x, y, *str);
        x += 6; // 5 pixels + 1 spacing
        str++;
    }
}

/* Draw a line (simple Bresenham) */
void oled_draw_line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        oled_draw_pixel(x0, y0, 1);

        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* Draw a rectangle */
void oled_draw_rect(int x, int y, int w, int h)
{
    oled_draw_line(x, y, x + w, y);
    oled_draw_line(x + w, y, x + w, y + h);
    oled_draw_line(x + w, y + h, x, y + h);
    oled_draw_line(x, y + h, x, y);
}

/* Draw a filled rectangle */
void oled_fill_rect(int x, int y, int w, int h)
{
    for (int i = x; i < x + w; i++) {
        for (int j = y; j < y + h; j++) {
            oled_draw_pixel(i, j, 1);
        }
    }
}

/* Display volume level with bar graph */
void oled_display_volume(uint8_t volume_level, bool muted, bool connected)
{
    char buf[32];

    oled_clear();

    /* Display title */
    oled_draw_string(20, 0, "HARC AUDIO");

    /* Display connection status */
    if (connected) {
        oled_draw_string(10, 12, "CONNECTED");
    } else {
        oled_draw_string(5, 12, "DISCONNECTED");
    }

    /* Display volume percentage */
    uint8_t volume_percent = (uint16_t)volume_level * 100 / 255;
    snprintk(buf, sizeof(buf), "VOL: %u%%", volume_percent);
    oled_draw_string(10, 26, buf);

    /* Display mute status */
    if (muted) {
        oled_draw_string(30, 38, "MUTED");
    }

    /* Draw volume bar */
    int bar_width = (int)((uint16_t)volume_level * 110 / 255);
    oled_draw_rect(8, 52, 112, 10);
    oled_fill_rect(9, 53, bar_width, 8);

    oled_display();
}

/* Display connection status */
void oled_display_status(const char *status)
{
    oled_clear();
    oled_draw_string(10, 20, "HARC AUDIO");
    oled_draw_string(10, 35, status);
    oled_display();
}

/* Show button press indicator */
void oled_show_button_indicator(const char *button_name)
{
    /* Draw button indicator at top right corner */
    oled_fill_rect(90, 0, 38, 10);

    /* Invert colors by drawing black text on white background */
    /* Note: This is a simple implementation - for true invert we'd need to XOR */
    oled_display();

    /* Alternative: Flash the entire display briefly */
    oled_draw_string(95, 1, button_name);
    oled_display();
}
