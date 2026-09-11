#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stddef.h>

#define SSD1306_I2C_ADDR    0x3C
#define SSD1306_WIDTH       128
#define SSD1306_HEIGHT      32
#define SSD1306_PAGES       (SSD1306_HEIGHT / 8)
#define SSD1306_BUFFER_SIZE (SSD1306_WIDTH * SSD1306_PAGES)

// Initialize the SSD1306 OLED display
int ssd1306_init(int fd);

// Turn display off (sleep mode on exit)
int ssd1306_display_off(int fd);

// Clear framebuffer (all black)
void ssd1306_clear(void);

// Drawing primitives
void ssd1306_draw_pixel(int x, int y, int color);
void ssd1306_draw_line_v(int x, int y0, int y1, int color);
void ssd1306_draw_line_h(int y, int x0, int x1, int color);
void ssd1306_draw_rect(int x, int y, int w, int h, int color);
void ssd1306_fill_rect(int x, int y, int w, int h, int color);

// Text rendering using 5x7 bitmap font
void ssd1306_draw_char(int x, int y, char c, int color);
void ssd1306_draw_string(int x, int y, const char *str, int color);

// Graphical progress bar (0 to 100%)
void ssd1306_draw_progress_bar(int x, int y, int w, int h, int percent);

// 16x16 1-bit bitmap rendering
void ssd1306_draw_bitmap_16x16(int x, int y, const uint8_t *bmp);

// Fan animation frames (4 frames: 0..3)
const uint8_t *ssd1306_get_fan_frame(int frame_idx);

// Flush framebuffer to SSD1306 via I2C
int ssd1306_flush(int fd);

// Render framebuffer as ASCII art in console for testing/debugging
void ssd1306_render_ascii_preview(void);

#endif // SSD1306_H
