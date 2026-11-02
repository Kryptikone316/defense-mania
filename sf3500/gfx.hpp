/* Small software renderer for the SF3500 build of Defense Mania: a 16-bit canvas shaped like the panel
 * (at most 640x480, the driver refuses bigger frames), alpha-blended images, shapes and text. */
#ifndef DM_GFX_HPP
#define DM_GFX_HPP

#include <stdint.h>
#include <string>
#include <vector>

struct Image {
	int w = 0, h = 0;
	std::vector<uint32_t> px;   /* 0xAARRGGBB, colour already graded */
	bool valid() const { return w > 0 && h > 0; }
};

extern int SW, SH;   /* canvas size in pixels */

/* ---- setup and frames ---- */
bool gfx_init();                         /* display, canvas, fonts */
void gfx_present();                      /* shows the canvas (or dumps it for the QEMU tests) */
void gfx_clear(uint8_t r, uint8_t g, uint8_t b);
unsigned gfx_pad();                      /* held buttons, one bit per JK_* of joykey.hpp */
uint32_t millis();

/* ---- settings ---- */
double cfg_num(const char *key, double def);   /* defensemania.cfg, key = value */
void grade_rgb(uint8_t &r, uint8_t &g, uint8_t &b);   /* the brightness/contrast/saturation/gamma table */
void grade_set_map(const char *mapName);       /* picks up map-specific overrides, e.g. woods_saturation */

/* ---- images ---- */
bool load_image(const std::string &path, Image &img, bool graded = true);
/* Draws the part (sx,sy,sw,sh) of img with its top-left at (dx,dy). alpha 0..255; tint multiplies the colour. */
void draw_image(const Image &img, int dx, int dy, int sx = 0, int sy = 0, int sw = -1, int sh = -1,
                int alpha = 255, uint32_t tint = 0xFFFFFF);
void draw_image_center(const Image &img, float cx, float cy, int sx = 0, int sy = 0, int sw = -1, int sh = -1,
                       int alpha = 255, uint32_t tint = 0xFFFFFF);
void draw_image_scaled(const Image &img, int dx, int dy, int dw, int dh, int alpha = 255);
void draw_image_rot(const Image &img, int sx, int sy, int sw, int sh, float cx, float cy, float angle, int alpha = 255);

/* An opaque image kept in the canvas's own 16-bit format, for the big map picture. */
struct Image565 {
	int w = 0, h = 0;
	std::vector<uint16_t> px;
};
bool load_image565(const std::string &path, Image565 &img);
void blit565_window(const Image565 &img, int sx, int sy);   /* the canvas-sized window at (sx,sy), clipped to the image */

/* ---- shapes (colour 0xRRGGBB) ---- */
void fill_rect(int x, int y, int w, int h, uint32_t rgb, int alpha = 255);
void rect_outline(int x, int y, int w, int h, uint32_t rgb, int alpha = 255);
void draw_line(int x0, int y0, int x1, int y1, uint32_t rgb, int alpha = 255);
void fill_circle(float cx, float cy, float r, uint32_t rgb, int alpha);
void circle_outline(float cx, float cy, float r, uint32_t rgb, int alpha = 255);

/* ---- text ---- */
enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };
/* font: 0 = Bitter (menus and titles), 1 = DejaVu Sans, 2 = DejaVu Sans Bold. Lines are split at '\n'. */
void draw_text(const std::string &s, int x, int y, int size, uint32_t rgb, int align = ALIGN_LEFT,
               int font = 0, int alpha = 255);
int text_width(const std::string &s, int size, int font = 0);
int text_lineheight(int size, int font = 0);

#endif
