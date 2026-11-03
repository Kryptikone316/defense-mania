#include "gfx.hpp"
#include "hwdisp.hpp"
#include "joykey.hpp"
#include "tfenv.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

int SW = 640, SH = 358;
static uint16_t *fb;
static uint16_t *fbRing[3];   /* frames are drawn into these in turn: the driver may still be reading the last one */
static int fbCount = 3, fbCur;

uint32_t millis() { return SDL_GetTicks(); }

/* ------------------------------------------------------------------ settings */
static std::map<std::string, double> g_cfg;

static void cfg_load()
{
	const char *home = getenv("HOME");
	char alt[512];
	FILE *f = fopen("defensemania.cfg", "r");
	if (!f && home)
	{
		snprintf(alt, sizeof alt, "%s/defensemania.cfg", home);
		f = fopen(alt, "r");
	}
	if (!f)
		return;
	char line[200];
	while (fgets(line, sizeof line, f))
	{
		char key[80];
		double v;
		if (line[0] != '#' && sscanf(line, " %79[A-Za-z0-9_] = %lf", key, &v) == 2)
			g_cfg[key] = v;
	}
	fclose(f);
}

double cfg_num(const char *key, double def)
{
	std::map<std::string, double>::const_iterator it = g_cfg.find(key);
	return it == g_cfg.end() ? def : it->second;
}

/* brightness, contrast, saturation, gamma: worked out per colour at load time, never while playing */
static uint8_t g_curve[256];
static int g_sat = 128;   /* 128 = unchanged */
static bool g_gradeOn;

void grade_set_map(const char *mapName)
{
	char key[96];
	#define MAPCFG(name, def) (snprintf(key, sizeof key, "%s_%s", mapName, name), cfg_num(key, cfg_num(name, def)))
	double brightness = MAPCFG("brightness", 0);
	double contrast = MAPCFG("contrast", 100);
	double saturation = MAPCFG("saturation", 100);
	double gamma = MAPCFG("gamma", 1.0);
	#undef MAPCFG
	if (gamma < 0.2) gamma = 0.2;
	if (gamma > 5) gamma = 5;
	g_gradeOn = !(brightness == 0 && contrast == 100 && saturation == 100 && gamma == 1.0);
	g_sat = (int)(saturation * 128 / 100 + 0.5);
	for (int i = 0; i < 256; i++)
	{
		double v = (i - 128) * contrast / 100.0 + 128 + brightness * 2.55;
		v = v < 0 ? 0 : v > 255 ? 255 : v;
		v = 255.0 * pow(v / 255.0, 1.0 / gamma);
		g_curve[i] = (uint8_t)(v + 0.5);
	}
}

void grade_rgb(uint8_t &r, uint8_t &g, uint8_t &b)
{
	if (!g_gradeOn)
		return;
	int lum = (77 * r + 150 * g + 29 * b) >> 8;
	int rr = lum + (((r - lum) * g_sat) >> 7);
	int gg = lum + (((g - lum) * g_sat) >> 7);
	int bb = lum + (((b - lum) * g_sat) >> 7);
	r = g_curve[rr < 0 ? 0 : rr > 255 ? 255 : rr];
	g = g_curve[gg < 0 ? 0 : gg > 255 ? 255 : gg];
	b = g_curve[bb < 0 ? 0 : bb > 255 ? 255 : bb];
}

/* ------------------------------------------------------------------ canvas */
static inline uint16_t pack565(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

static inline void blend_px(uint16_t *d, int r, int g, int b, int a)   /* a 1..254 */
{
	int dv = *d;
	int dr = (dv >> 11) << 3 | (dv >> 13), dg = ((dv >> 5) & 63) << 2 | ((dv >> 9) & 3), db = (dv & 31) << 3 | ((dv >> 2) & 7);
	dr += ((r - dr) * a) / 255;
	dg += ((g - dg) * a) / 255;
	db += ((b - db) * a) / 255;
	*d = pack565(dr, dg, db);
}

void gfx_clear(uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t c = pack565(r, g, b);
	for (int i = 0; i < SW * SH; i++)
		fb[i] = c;
}

/* ------------------------------------------------------------------ pad, frames, test hooks */
static unsigned pad_script(unsigned bits)
{
	static int init, frame, n;
	static int when[512];
	static unsigned masks[512];
	static const char *script;
	if (!init)
	{
		init = 1;
		script = getenv("DM_PAD");
		const char *s = script;
		while (s && *s && n < 512)
		{
			when[n] = (int)strtol(s, (char **)&s, 10);
			if (*s != ':')
				break;
			masks[n++] = (unsigned)strtoul(s + 1, (char **)&s, 0);
			if (*s == ',')
				s++;
		}
	}
	if (script)
	{
		static unsigned cur;
		for (int i = 0; i < n; i++)
			if (when[i] == frame)
				cur = masks[i];
		frame++;
		bits |= cur;
	}
	return bits;
}

unsigned gfx_pad() { return pad_script(joykey_read()); }

static void dump_frame(int frame)
{
	static const char *dir = getenv("DM_SHOTDIR");
	static int list[64], nlist = -1;
	if (!dir)
		return;
	if (nlist < 0)
	{
		nlist = 0;
		const char *s = getenv("DM_SHOTS");
		while (s && *s && nlist < 64)
		{
			list[nlist++] = (int)strtol(s, (char **)&s, 10);
			if (*s == ',')
				s++;
		}
	}
	for (int i = 0; i < nlist; i++)
		if (list[i] == frame)
		{
			char path[600];
			snprintf(path, sizeof path, "%s/frame%05d.ppm", dir, frame);
			FILE *f = fopen(path, "wb");
			if (!f)
				return;
			fprintf(f, "P6\n%d %d\n255\n", SW, SH);
			for (int p = 0; p < SW * SH; p++)
			{
				uint16_t v = fb[p];
				uint8_t rgb[3] = { (uint8_t)((v >> 11) << 3 | (v >> 13)), (uint8_t)(((v >> 5) & 63) << 2 | ((v >> 9) & 3)), (uint8_t)((v & 31) << 3 | ((v >> 2) & 7)) };
				fwrite(rgb, 1, 3, f);
			}
			fclose(f);
		}
}

/* The canvas pointer and size must never change after start-up. If something wrote over them, say so in log.txt (once)
 * and put them back, so the match goes on and the report shows what was overwritten. */
static void check_canvas()
{
	static uint16_t *good[3];
	static int swGood, shGood;
	if (!good[0])
	{
		for (int i = 0; i < 3; i++) good[i] = fbRing[i];
		swGood = SW;
		shGood = SH;
		return;
	}
	bool bad = fb != good[fbCur] || SW != swGood || SH != shGood;
	for (int i = 0; i < fbCount; i++)
		if (fbRing[i] != good[i]) bad = true;
	if (bad)
	{
		static int told;
		if (told++ < 3)
		{
			printf("memory overwritten: canvas %p %dx%d should be %p %dx%d\n", (void *)fb, SW, SH, (void *)good[fbCur], swGood, shGood);
			fflush(stdout);
		}
		for (int i = 0; i < 3; i++) fbRing[i] = good[i];
		fb = good[fbCur];
		SW = swGood;
		SH = shGood;
	}
}

void gfx_canvas_info(unsigned long out[3]) { out[0] = (unsigned long)fb; out[1] = (unsigned long)SW; out[2] = (unsigned long)SH; }

void gfx_present()
{
	static int frame;
	check_canvas();
	if (hwdisp_active())
		hwdisp_present(fb, SW, SH, SW * 2);
	else
		dump_frame(frame);
	frame++;
	if (fbCount > 1)   /* the next frame goes into another buffer */
	{
		fbCur = (fbCur + 1) % fbCount;
		fb = fbRing[fbCur];
	}
}

bool gfx_init()
{
	if (!getenv("SDL_VIDEODRIVER"))
		putenv((char *)"SDL_VIDEODRIVER=dummy");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE) < 0)
	{
		printf("SDL_Init failed: %s\n", SDL_GetError());
		return false;
	}
	if (TTF_Init() != 0)
	{
		printf("TTF_Init failed\n");
		return false;
	}
	hwdisp_init();
	joykey_init();
	char a[32], b[32];
	int pw = 854, ph = 480;
	if (tfenv_get("TF_PANEL_W", a, sizeof a) && tfenv_get("TF_PANEL_H", b, sizeof b))
	{
		pw = atoi(a);
		ph = atoi(b);
	}
	if (pw < ph) { int t = pw; pw = ph; ph = t; }
	if (pw < 160 || ph < 120)
	{
		pw = 854;
		ph = 480;
	}
	SW = 640;
	SH = 640 * ph / pw;
	if (SH > 480)
	{
		SH = 480;
		SW = 480 * pw / ph;
	}
	SW &= ~1;
	SH &= ~1;
	cfg_load();
	grade_set_map("");
	fbCount = (int)cfg_num("frame_buffers", 3);
	if (fbCount < 1) fbCount = 1;
	if (fbCount > 3) fbCount = 3;
	for (int i = 0; i < 3; i++)
		fbRing[i] = (uint16_t *)calloc((size_t)SW * SH, sizeof(uint16_t));
	fb = fbRing[0];
	return fbRing[0] && fbRing[1] && fbRing[2];
}

/* ------------------------------------------------------------------ images */
bool load_image(const std::string &path, Image &img, bool graded)
{
	int w, h, n;
	unsigned char *d = stbi_load(path.c_str(), &w, &h, &n, 4);
	if (!d)
	{
		printf("cannot load %s\n", path.c_str());
		return false;
	}
	img.w = w;
	img.h = h;
	img.px.resize((size_t)w * h);
	for (int i = 0; i < w * h; i++)
	{
		uint8_t r = d[i * 4], g = d[i * 4 + 1], b = d[i * 4 + 2], a = d[i * 4 + 3];
		if (graded && a)
			grade_rgb(r, g, b);
		img.px[i] = (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
	}
	stbi_image_free(d);
	return true;
}

bool load_image565(const std::string &path, Image565 &img)
{
	int w, h, n;
	unsigned char *d = stbi_load(path.c_str(), &w, &h, &n, 3);
	if (!d)
	{
		printf("cannot load %s\n", path.c_str());
		return false;
	}
	img.w = w;
	img.h = h;
	img.px.resize((size_t)w * h);
	for (int i = 0; i < w * h; i++)
	{
		uint8_t r = d[i * 3], g = d[i * 3 + 1], b = d[i * 3 + 2];
		grade_rgb(r, g, b);
		img.px[i] = pack565(r, g, b);
	}
	stbi_image_free(d);
	return true;
}

void blit565_window(const Image565 &img, int sx, int sy)
{
	for (int y = 0; y < SH; y++)
	{
		int iy = sy + y;
		if (iy < 0 || iy >= img.h)
			continue;
		int x0 = sx < 0 ? -sx : 0, x1 = sx + SW > img.w ? img.w - sx : SW;
		if (x1 > x0)
			memcpy(&fb[(size_t)y * SW + x0], &img.px[(size_t)iy * img.w + sx + x0], (size_t)(x1 - x0) * 2);
	}
}

void draw_image(const Image &img, int dx, int dy, int sx, int sy, int sw, int sh, int alpha, uint32_t tint)
{
	if (!img.valid() || alpha <= 0)
		return;
	if (sw < 0) sw = img.w - sx;
	if (sh < 0) sh = img.h - sy;
	int x0 = 0, y0 = 0, x1 = sw, y1 = sh;
	if (dx < 0) x0 = -dx;
	if (dy < 0) y0 = -dy;
	if (dx + x1 > SW) x1 = SW - dx;
	if (dy + y1 > SH) y1 = SH - dy;
	bool tinted = (tint & 0xFFFFFF) != 0xFFFFFF;
	int tr = (tint >> 16) & 255, tg = (tint >> 8) & 255, tb = tint & 255;
	for (int y = y0; y < y1; y++)
	{
		const uint32_t *src = &img.px[(size_t)(sy + y) * img.w + sx];
		uint16_t *dst = &fb[(size_t)(dy + y) * SW + dx];
		for (int x = x0; x < x1; x++)
		{
			uint32_t p = src[x];
			int a = p >> 24;
			if (!a)
				continue;
			if (alpha != 255)
				a = a * alpha / 255;
			if (!a)
				continue;
			int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
			if (tinted)
			{
				r = r * tr / 255;
				g = g * tg / 255;
				b = b * tb / 255;
			}
			if (a >= 250)
				dst[x] = pack565(r, g, b);
			else
				blend_px(&dst[x], r, g, b, a);
		}
	}
}

void draw_image_center(const Image &img, float cx, float cy, int sx, int sy, int sw, int sh, int alpha, uint32_t tint)
{
	if (sw < 0) sw = img.w - sx;
	if (sh < 0) sh = img.h - sy;
	draw_image(img, (int)floorf(cx - sw / 2.0f + 0.5f), (int)floorf(cy - sh / 2.0f + 0.5f), sx, sy, sw, sh, alpha, tint);
}

void draw_image_scaled(const Image &img, int dx, int dy, int dw, int dh, int alpha)
{
	if (!img.valid() || dw <= 0 || dh <= 0)
		return;
	for (int y = 0; y < dh; y++)
	{
		int py = dy + y;
		if (py < 0 || py >= SH)
			continue;
		const uint32_t *row = &img.px[(size_t)(y * img.h / dh) * img.w];
		for (int x = 0; x < dw; x++)
		{
			int pxx = dx + x;
			if (pxx < 0 || pxx >= SW)
				continue;
			uint32_t p = row[x * img.w / dw];
			int a = (p >> 24) * alpha / 255;
			if (!a)
				continue;
			int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
			if (a >= 250)
				fb[(size_t)py * SW + pxx] = pack565(r, g, b);
			else
				blend_px(&fb[(size_t)py * SW + pxx], r, g, b, a);
		}
	}
}

void draw_image_rot(const Image &img, int sx, int sy, int sw, int sh, float cx, float cy, float angle, int alpha)
{
	if (!img.valid())
		return;
	if (!(cx - cx == 0) || !(cy - cy == 0) || !(angle - angle == 0) || cx < -4000 || cx > 4000 || cy < -4000 || cy > 4000)
		return;   /* not a place to draw */
	if (sw <= 0 || sh <= 0 || sx < 0 || sy < 0 || sx + sw > img.w || sy + sh > img.h)
		return;   /* not a part of this image */
	float c = cosf(angle), s = sinf(angle);
	float half = (sw > sh ? sw : sh) * 0.75f;
	int x0 = (int)(cx - half), x1 = (int)(cx + half) + 1, y0 = (int)(cy - half), y1 = (int)(cy + half) + 1;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > SW) x1 = SW;
	if (y1 > SH) y1 = SH;
	for (int y = y0; y < y1; y++)
	{
		if (y < 0 || y >= SH)
			continue;
		for (int x = x0; x < x1; x++)
		{
			if (x < 0 || x >= SW)
				continue;
			float fx = x + 0.5f - cx, fy = y + 0.5f - cy;
			float u = fx * c + fy * s + sw / 2.0f, v = -fx * s + fy * c + sh / 2.0f;
			int iu = (int)floorf(u), iv = (int)floorf(v);
			if (iu < 0 || iv < 0 || iu >= sw || iv >= sh)
				continue;
			uint32_t p = img.px[(size_t)(sy + iv) * img.w + sx + iu];
			int a = (p >> 24) * alpha / 255;
			if (!a)
				continue;
			int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
			if (a >= 250)
				fb[(size_t)y * SW + x] = pack565(r, g, b);
			else
				blend_px(&fb[(size_t)y * SW + x], r, g, b, a);
		}
	}
}

/* ------------------------------------------------------------------ shapes */
void fill_rect(int x, int y, int w, int h, uint32_t rgb, int alpha)
{
	int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > SW ? SW : x + w, y1 = y + h > SH ? SH : y + h;
	uint16_t solid = pack565(r, g, b);
	for (int yy = y0; yy < y1; yy++)
		for (int xx = x0; xx < x1; xx++)
		{
			if (alpha >= 250)
				fb[(size_t)yy * SW + xx] = solid;
			else if (alpha > 0)
				blend_px(&fb[(size_t)yy * SW + xx], r, g, b, alpha);
		}
}

void rect_outline(int x, int y, int w, int h, uint32_t rgb, int alpha)
{
	fill_rect(x, y, w, 1, rgb, alpha);
	fill_rect(x, y + h - 1, w, 1, rgb, alpha);
	fill_rect(x, y + 1, 1, h - 2, rgb, alpha);
	fill_rect(x + w - 1, y + 1, 1, h - 2, rgb, alpha);
}

void draw_line(int x0, int y0, int x1, int y1, uint32_t rgb, int alpha)
{
	int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
	for (;;)
	{
		fill_rect(x0, y0, 1, 1, rgb, alpha);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

static void circle(float cx, float cy, float r, uint32_t rgb, int alpha, bool filled)
{
	int rr = (int)(rgb >> 16 & 255), gg = (int)(rgb >> 8 & 255), bb = (int)(rgb & 255);
	int x0 = (int)(cx - r - 1), x1 = (int)(cx + r + 2), y0 = (int)(cy - r - 1), y1 = (int)(cy + r + 2);
	for (int y = y0 < 0 ? 0 : y0; y < (y1 > SH ? SH : y1); y++)
		for (int x = x0 < 0 ? 0 : x0; x < (x1 > SW ? SW : x1); x++)
		{
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
			bool on = filled ? d <= r : fabsf(d - r) <= 0.8f;
			if (on)
			{
				if (alpha >= 250)
					fb[(size_t)y * SW + x] = pack565(rr, gg, bb);
				else
					blend_px(&fb[(size_t)y * SW + x], rr, gg, bb, alpha);
			}
		}
}
void fill_circle(float cx, float cy, float r, uint32_t rgb, int alpha) { circle(cx, cy, r, rgb, alpha, true); }
void circle_outline(float cx, float cy, float r, uint32_t rgb, int alpha) { circle(cx, cy, r, rgb, alpha, false); }

/* ------------------------------------------------------------------ text */
static TTF_Font *get_font(int font, int size)
{
	static std::map<int, TTF_Font *> fonts;
	int key = font * 1000 + size;
	std::map<int, TTF_Font *>::iterator it = fonts.find(key);
	if (it != fonts.end())
		return it->second;
	static const char *files[3] = { "data/bitterregular.ttf", "data/DejaVuSans.ttf", "data/DejaVuSans-Bold.ttf" };
	TTF_Font *f = TTF_OpenFont(files[font], size);
	if (!f)
		printf("cannot open font %s\n", files[font]);
	else
		TTF_SetFontHinting(f, TTF_HINTING_MONO);
	fonts[key] = f;
	return f;
}

int text_lineheight(int size, int font)
{
	TTF_Font *f = get_font(font, size);
	return f ? TTF_FontLineSkip(f) : size;
}

int text_width(const std::string &s, int size, int font)
{
	TTF_Font *f = get_font(font, size);
	if (!f)
		return 0;
	int best = 0;
	size_t start = 0;
	while (start <= s.size())
	{
		size_t nl = s.find('\n', start);
		std::string line = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
		int w = 0, h = 0;
		if (!line.empty())
			TTF_SizeText(f, line.c_str(), &w, &h);
		if (w > best)
			best = w;
		if (nl == std::string::npos)
			break;
		start = nl + 1;
	}
	return best;
}

void draw_text(const std::string &s, int x, int y, int size, uint32_t rgb, int align, int font, int alpha, int outline)
{
	TTF_Font *f = get_font(font, size);
	if (!f)
		return;
	SDL_Color col = { (Uint8)(rgb >> 16 & 255), (Uint8)(rgb >> 8 & 255), (Uint8)(rgb & 255), 0 };
	int lh = TTF_FontLineSkip(f);
	size_t start = 0;
	int row = 0;
	while (start <= s.size())
	{
		size_t nl = s.find('\n', start);
		std::string line = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
		if (!line.empty())
		{
			SDL_Surface *t = TTF_RenderText_Solid(f, line.c_str(), col);
			if (t)
			{
				int dx = x, dy = y + row * lh;
				if (align == ALIGN_CENTER) dx = x - t->w / 2;
				else if (align == ALIGN_RIGHT) dx = x - t->w;
				SDL_LockSurface(t);
				static const int ring[8][2] = { {-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1} };
				for (int pass = outline >= 0 ? 0 : 1; pass < 2; pass++)
					for (int o = 0; o < (pass == 0 ? 8 : 1); o++)
					{
						int ox = pass == 0 ? ring[o][0] : 0, oy = pass == 0 ? ring[o][1] : 0;
						int r = pass == 0 ? (outline >> 16 & 255) : col.r, g = pass == 0 ? (outline >> 8 & 255) : col.g, b = pass == 0 ? (outline & 255) : col.b;
						for (int yy = 0; yy < t->h; yy++)
						{
							int py = dy + yy + oy;
							if (py < 0 || py >= SH)
								continue;
							const Uint8 *src = (const Uint8 *)t->pixels + yy * t->pitch;
							for (int xx = 0; xx < t->w; xx++)
							{
								int pxx = dx + xx + ox;
								if (src[xx] && pxx >= 0 && pxx < SW)
								{
									if (alpha >= 250)
										fb[(size_t)py * SW + pxx] = pack565(r, g, b);
									else
										blend_px(&fb[(size_t)py * SW + pxx], r, g, b, alpha);
								}
							}
						}
					}
				SDL_UnlockSurface(t);
				SDL_FreeSurface(t);
			}
		}
		if (nl == std::string::npos)
			break;
		start = nl + 1;
		row++;
	}
}
