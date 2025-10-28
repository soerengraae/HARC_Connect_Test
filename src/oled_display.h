/*
 * OLED Display Driver for JMD0.96C (SSD1306)
 * Header file
 */

#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/* OLED Display dimensions */
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

/* Initialize OLED display */
int oled_display_init(void);

/* Clear display buffer */
void oled_clear(void);

/* Update display with buffer contents */
void oled_display(void);

/* Draw a pixel at (x, y) */
void oled_draw_pixel(int x, int y, uint8_t color);

/* Draw a character at (x, y) */
void oled_draw_char(int x, int y, char c);

/* Draw a string at (x, y) */
void oled_draw_string(int x, int y, const char *str);

/* Draw a line from (x0, y0) to (x1, y1) */
void oled_draw_line(int x0, int y0, int x1, int y1);

/* Draw a rectangle at (x, y) with width w and height h */
void oled_draw_rect(int x, int y, int w, int h);

/* Draw a filled rectangle */
void oled_fill_rect(int x, int y, int w, int h);

/* Display volume level with bar graph */
void oled_display_volume(uint8_t volume_level, bool muted, bool connected);

/* Display connection status */
void oled_display_status(const char *status);

/* Show button press indicator (UP or DOWN) */
void oled_show_button_indicator(const char *button_name);

#endif /* OLED_DISPLAY_H */
