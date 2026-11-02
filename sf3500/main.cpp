/* Defense Mania for the Data Frog SF3500: menus, the pad cursor and the main loop. */
#include "game.hpp"
#include "hwdisp.hpp"
#include "joykey.hpp"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const float PI_F = 3.14159265f;
static const uint32_t GOLD = 0xFFF8C8;

enum Screen { S_MAIN, S_PLAY, S_OPTIONS, S_CREDITS, S_LOADING, S_GAME, S_AFTER, S_QUIT };

/* ------------------------------------------------------------------ pad */
static unsigned padNow, padPrev;
static inline bool held(int jk) { return (padNow >> jk) & 1; }
static inline bool pressed(int jk) { return held(jk) && !((padPrev >> jk) & 1); }

/* ------------------------------------------------------------------ menu pieces */
struct MapInfo { std::string name, url, size; Image logo; };
static std::vector<MapInfo> maps;
static Image logo;
static int difficulty = 2;       /* 1 easy, 2 normal, 3 hard */
static int mapPicked = 0;
static int lastMap = 0, lastDifficulty = 2;

static void draw_button(const char *label, int x, int y, int w, int h, bool active, int size = 20)
{
	uint32_t light = active ? 0x89F9FF : 0xF8DDC2, dark = active ? 0x001807 : 0x251002;
	fill_rect(x, y + h - 1, w, 1, light);
	fill_rect(x, y + h, w, 1, dark);
	draw_text(label, x + w / 2, y + (h - size) / 2 - 2, size, active ? 0x89F9FF : 0xD0D0D0, ALIGN_CENTER, 0);
}

static void draw_hint(const char *s) { draw_text(s, SW / 2, SH - 16, 10, 0xB0B0B0, ALIGN_CENTER, 1); }

static void load_maps()
{
	XNode root;
	if (!xml_load("data/maps.xml", root))
		return;
	std::vector<const XNode *> ms = root.list("standardmaps/map");
	for (size_t i = 0; i < ms.size(); i++)
	{
		MapInfo m;
		m.name = ms[i]->str("name");
		m.url = ms[i]->str("url");
		m.size = ms[i]->str("size");
		load_image("data/maps/" + m.url + "/" + ms[i]->str("img"), m.logo);
		maps.push_back(m);
	}
}

static Screen screen_main(int &sel)
{
	static const char *labels[4] = { "PLAY", "CREDITS", "OPTIONS", "EXIT" };
	gfx_clear(0, 0, 0);
	if (logo.valid())
	{
		int w = logo.w * 62 / 100, h = logo.h * 62 / 100;
		draw_image_scaled(logo, (SW - w) / 2, 8, w, h);
	}
	int bw = 130, gap = (SW - bw * 4) / 5;
	for (int i = 0; i < 4; i++)
		draw_button(labels[i], gap + i * (bw + gap), SH - 62, bw, 26, i == sel, 22);
	draw_hint("D-Pad: choose    A: select");
	if (pressed(JK_LEFT)) sel = (sel + 3) % 4;
	if (pressed(JK_RIGHT)) sel = (sel + 1) % 4;
	if (pressed(JK_A))
	{
		if (sel == 0) return S_PLAY;
		if (sel == 1) return S_CREDITS;
		if (sel == 2) return S_OPTIONS;
		return S_QUIT;
	}
	return S_MAIN;
}

static Screen screen_play()
{
	gfx_clear(0, 0, 0);
	static const char *dif[3] = { "EASY", "NORMAL", "HARD" };
	int bw = 110, gap = (SW - bw * 3) / 4;
	for (int i = 0; i < 3; i++)
		draw_button(dif[i], gap + i * (bw + gap), 22, bw, 26, difficulty == i + 1, 20);
	int listX = 70;
	draw_text("Standard maps", listX, 66, 16, 0xFFFFFF, ALIGN_LEFT, 0, 190);
	fill_rect(listX, 88, 300, 1, 0xF8DDC2);
	fill_rect(listX, 89, 300, 1, 0x251002);
	for (size_t i = 0; i < maps.size(); i++)
	{
		int y = 98 + (int)i * 40;
		bool on = (int)i == mapPicked;
		draw_text(maps[i].name, listX + 8, y + 2, 18, on ? 0x89F9FF : 0xD0D0D0, ALIGN_LEFT, 0);
		draw_text(maps[i].size, listX + 292, y + 6, 11, on ? 0x89F9FF : 0x909090, ALIGN_RIGHT, 1);
		if (on)
			rect_outline(listX - 4, y - 4, 308, 34, 0x89F9FF, 150);
	}
	if (mapPicked >= 0 && mapPicked < (int)maps.size() && maps[mapPicked].logo.valid())
		draw_image(maps[mapPicked].logo, SW - 70 - maps[mapPicked].logo.w, 98);
	draw_button("BACK", 40, SH - 54, 110, 26, false, 20);
	draw_button("PLAY", SW - 150, SH - 54, 110, 26, true, 20);
	draw_hint("Up/Down: map    Left/Right: difficulty    A: play    B: back");
	if (pressed(JK_UP) && mapPicked > 0) mapPicked--;
	if (pressed(JK_DOWN) && mapPicked + 1 < (int)maps.size()) mapPicked++;
	if (pressed(JK_LEFT) && difficulty > 1) difficulty--;
	if (pressed(JK_RIGHT) && difficulty < 3) difficulty++;
	if (pressed(JK_B)) return S_MAIN;
	if (pressed(JK_A) && !maps.empty()) return S_LOADING;
	return S_PLAY;
}

static Screen screen_options(int &sel)
{
	gfx_clear(0, 0, 0);
	const char *name[2] = { "HQ GRAPHICS", "SHOW FPS" };
	const char *help[2] = { "Adds a light overlay on the map: this costs\nperformance. Takes effect on the next match.", "Shows the current frame rate." };
	bool on[2] = { optHQ, optShowFps };
	for (int i = 0; i < 2; i++)
	{
		int y = 50 + i * 76;
		draw_button(name[i], 40, y, 190, 26, i == sel || on[i], 20);
		draw_text(on[i] ? "ON" : "OFF", 250, y + 3, 16, on[i] ? 0x89F9FF : 0x909090, ALIGN_LEFT, 0);
		draw_text(help[i], 300, y + 2, 11, 0xE6E6E6, ALIGN_LEFT, 1);
		if (i == sel)
			rect_outline(34, y - 6, SW - 68, 40, 0x89F9FF, 120);
	}
	draw_button("BACK", 40, SH - 54, 110, 26, false, 20);
	draw_hint("Up/Down: choose    A: switch    B: back");
	if (pressed(JK_UP)) sel = 0;
	if (pressed(JK_DOWN)) sel = 1;
	if (pressed(JK_A))
	{
		if (sel == 0) optHQ = !optHQ; else optShowFps = !optShowFps;
	}
	if (pressed(JK_B)) return S_MAIN;
	return S_OPTIONS;
}

static Screen screen_credits()
{
	gfx_clear(0, 0, 0);
	draw_text("Developed and created by Kris Siepert.\n\nBased on Processing & ProcessingJS.\n\nThis port runs the original game's logic and data\nnatively on the Data Frog SF3500.",
	          70, 80, 14, 0xE6E6E6, ALIGN_LEFT, 0);
	draw_button("BACK", 40, SH - 54, 110, 26, true, 20);
	draw_hint("B: back");
	if (pressed(JK_B) || pressed(JK_A) || pressed(JK_START)) return S_MAIN;
	return S_CREDITS;
}

static Screen screen_after(bool won, int &sel)
{
	gfx_clear(0, 0, 0);
	draw_text(won ? "WELL DONE!" : "MAYBE NEXT TIME ...", SW / 2, SH / 2 - 50, 36, 0xFFFFFF, ALIGN_CENTER, 0);
	static const char *labels[3] = { "PLAY AGAIN", "BACK TO MENU", "EXIT GAME" };
	int bw = 160, gap = (SW - bw * 3) / 4;
	for (int i = 0; i < 3; i++)
		draw_button(labels[i], gap + i * (bw + gap), SH - 62, bw, 26, i == sel, 18);
	draw_hint("Left/Right: choose    A: select");
	if (pressed(JK_LEFT)) sel = (sel + 2) % 3;
	if (pressed(JK_RIGHT)) sel = (sel + 1) % 3;
	if (pressed(JK_A))
	{
		if (sel == 0) { mapPicked = lastMap; difficulty = lastDifficulty; return S_LOADING; }
		if (sel == 1) return S_PLAY;
		return S_QUIT;
	}
	return S_AFTER;
}

/* ------------------------------------------------------------------ playing: cursor and radial menus */
struct Play {
	Map *map = NULL;
	float cx = 0, cy = 0, holdMs = 0;
	int menuKind = 0;       /* 0 none, 1 build, 2 tower */
	int menuSel = 0, menuTX = 0, menuTY = 0;
	Tower *menuTower = NULL;
	int started = 0;
	int quitSel = 1;
	int speed2 = 0;
};

static Image repairIcon, updateIcon, sellIcon;

static void clamp_camera()
{
	Map *m = NULL; (void)m;
}

static void camera_limits(const Map *m)
{
	int maxX = m->mapW - SW, maxY = m->mapH - SH;
	if (maxX < 0) maxX = 0;
	if (maxY < 0) maxY = 0;
	if (camX < 0) camX = 0;
	if (camY < 0) camY = 0;
	if (camX > maxX) camX = maxX;
	if (camY > maxY) camY = maxY;
}

static void radial_geometry(const Play &p, int count, int iconSize, float &x, float &y, float &radius, int tx, int ty)
{
	Map *m = p.map;
	radius = (float)(int)((iconSize * 1.5 * count) / (2 * PI_F));
	float minR = iconSize * (p.menuKind == 1 ? 0.6f : 1.1f);
	if (radius < minR) radius = (float)(int)minR;
	float dx = 0, dy = 0;
	int edge = (int)(radius + iconSize / 2) - m->tileSize / 2;
	if (tx == 0) dx = (float)edge;
	if (tx == m->cols - 1) dx = (float)-edge;
	if (ty == 0) dy = (float)edge;
	if (ty == m->rows - 1) dy = (float)-edge;
	x = (tx + 0.5f) * m->tileSize + dx - camX;
	y = (ty + 0.5f) * m->tileSize + dy - camY;
	float pad = radius + iconSize / 2 + 3;
	if (x < pad) x = pad;
	if (x > SW - pad) x = SW - pad;
	if (y < pad) y = pad;
	if (y > SH - pad) y = SH - pad;
}

static void info_box(const std::string &text)
{
	int w = text_width(text, 10, 1) + 14, h = text_lineheight(10, 1) * 2 + 8;
	int x = SW - 10 - w, y = SH - 64 - h;
	fill_rect(x, y, w, h, 0x000000, 170);
	draw_text(text, x + 7, y + 4, 10, 0xFFFFFF, ALIGN_LEFT, 1);
}

static void draw_radial(const Play &p)
{
	Map *m = p.map;
	int count = p.menuKind == 1 ? (int)m->towerTypes.size() : 3;
	int iconSize = p.menuKind == 1 ? m->towerTypes[0].iconSize : 30;
	float x, y, r;
	radial_geometry(p, count, iconSize, x, y, r, p.menuTX, p.menuTY);
	circle_outline(x, y, r, 0xCCFF99, 200);
	for (int i = 0; i < count; i++)
	{
		float ix = x + sinf(2 * PI_F / count * i) * r, iy = y + cosf(2 * PI_F / count * i) * r;
		const Image *img;
		bool dim;
		int cost;
		if (p.menuKind == 1)
		{
			img = &m->towerTypes[i].iconImg;
			cost = m->towerTypes[i].costs;
			dim = m->money < cost;
		}
		else
		{
			Tower *t = p.menuTower;
			img = i == 0 ? &repairIcon : i == 1 ? &updateIcon : &sellIcon;
			cost = 0;
			dim = false;
			if (i == 0) { if (t->health != t->maxHealth) cost = t->repairCosts(); dim = t->health == t->maxHealth || m->money < t->repairCosts(); }
			if (i == 1) { if (t->updates > 0 && t->currentUpdate != t->updates) cost = t->ucosts[t->currentUpdate]; dim = t->updates == 0 || t->currentUpdate == t->updates || m->money < t->ucosts[t->currentUpdate]; }
			if (i == 2) cost = (int)(t->costs * 0.8);
		}
		if (i == p.menuSel)
			fill_circle(ix, iy, iconSize / 2 + 5.0f, 0x89F9FF, 90);
		draw_image_center(*img, ix, iy, 0, 0, -1, -1, dim ? 200 : 255, dim ? 0x969696 : 0xFFFFFF);
		char b[24];
		snprintf(b, sizeof b, "%d $", cost);
		fill_rect((int)(ix - iconSize * 0.4f), (int)(iy + iconSize / 2 - 2), (int)(iconSize * 0.8f), 11, 0x000000, 190);
		draw_text(b, (int)ix, (int)(iy + iconSize / 2 - 2), 9, 0xFFFFFF, ALIGN_CENTER, 1);
	}
	if (p.menuKind == 1)
	{
		const TowerType &t = m->towerTypes[p.menuSel];
		char b[200];
		snprintf(b, sizeof b, "%s - %d $\nDMG: %d   FR: %.1fs   %s", t.name.c_str(), t.costs, t.damage, (float)t.fireRate / 1000, t.description.c_str());
		info_box(b);
	}
	else if (p.menuTower)
		info_box(p.menuTower->name + "\n" + p.menuTower->description());
}

static void close_menu(Play &p)
{
	if (p.menuTower)
	{
		p.menuTower->showRadius = false;
		p.menuTower->selected = false;
	}
	p.menuKind = 0;
	p.menuTower = NULL;
}

static void open_tower_menu(Play &p, int id)
{
	p.menuKind = 2;
	p.menuSel = 1;
	p.menuTower = p.map->towers[id];
	p.menuTower->showRadius = true;
	p.menuTower->selected = true;
	p.menuTX = p.menuTower->position[0];
	p.menuTY = p.menuTower->position[1];
}

static void play_input(Play &p, float dtMs)
{
	Map *m = p.map;
	if (m->quitDialog)
	{
		if (pressed(JK_LEFT) || pressed(JK_RIGHT)) p.quitSel = 1 - p.quitSel;
		if (pressed(JK_B))
			p.quitSel = 0;
		if (pressed(JK_A) || pressed(JK_B))
		{
			if (p.quitSel == 1)
				m->finished = true;   /* the "YES" path: leave the match */
			else
			{
				m->quitDialog = false;
				m->pause = false;
				globalSpeed = p.speed2 ? 2.0f : 1.0f;
			}
			if (p.quitSel == 1) m->finishedWon = false;
		}
		return;
	}
	if (pressed(JK_START))
	{
		m->pause = !m->pause;
		globalSpeed = m->pause ? 0.0f : (p.speed2 ? 2.0f : 1.0f);
	}
	if (pressed(JK_SELECT))
	{
		close_menu(p);
		m->quitDialog = true;
		m->pause = true;
		globalSpeed = 0;
		p.quitSel = 0;
		return;
	}
	if (pressed(JK_R1) && !m->pause && !m->defeated)
	{
		p.speed2 = !p.speed2;
		globalSpeed = p.speed2 ? 2.0f : 1.0f;
	}
	if (pressed(JK_X))
		m->showBuildAreas = !m->showBuildAreas;
	if (m->pause || m->defeated)
		return;
	if (p.menuKind)
	{
		int count = p.menuKind == 1 ? (int)m->towerTypes.size() : 3;
		if (pressed(JK_LEFT) || pressed(JK_UP)) p.menuSel = (p.menuSel + count - 1) % count;
		if (pressed(JK_RIGHT) || pressed(JK_DOWN)) p.menuSel = (p.menuSel + 1) % count;
		if (pressed(JK_B)) close_menu(p);
		else if (pressed(JK_A))
		{
			if (p.menuKind == 1)
			{
				m->addTower(p.menuSel, p.menuTX, p.menuTY);
				p.menuKind = 0;
			}
			else
			{
				Tower *t = p.menuTower;
				if (p.menuSel == 2) { close_menu(p); m->sellTower(t); }
				else if (p.menuSel == 0) t->repair();
				else t->upgrade();
			}
		}
		return;
	}
	/* the cursor: D-pad, faster the longer it is held; near the screen edge it scrolls the map */
	float dx = 0, dy = 0;
	if (held(JK_LEFT)) dx -= 1;
	if (held(JK_RIGHT)) dx += 1;
	if (held(JK_UP)) dy -= 1;
	if (held(JK_DOWN)) dy += 1;
	if (dx || dy)
	{
		p.holdMs += dtMs;
		float speed = 110 + (p.holdMs > 1100 ? 1100 : p.holdMs) * 0.32f;
		float step = speed * dtMs / 1000.0f;
		float nx = p.cx + dx * step, ny = p.cy + dy * step;
		const float margin = 34;
		if (nx < margin) { camX += (int)floorf(nx - margin); nx = margin; }
		if (nx > SW - margin) { camX += (int)ceilf(nx - (SW - margin)); nx = SW - margin; }
		if (ny < margin) { camY += (int)floorf(ny - margin); ny = margin; }
		if (ny > SH - margin) { camY += (int)ceilf(ny - (SH - margin)); ny = SH - margin; }
		p.cx = nx;
		p.cy = ny;
		camera_limits(m);
	}
	else
		p.holdMs = 0;
	if (pressed(JK_A) && !m->defeated)
	{
		int wx = (int)p.cx + camX, wy = (int)p.cy + camY;
		int j = m->towerAt(wx, wy);
		if (j != -1)
			open_tower_menu(p, j);
		else if (m->canBuild(wx, wy))
		{
			p.menuKind = 1;
			p.menuSel = 0;
			p.menuTX = wx / m->tileSize;
			p.menuTY = wy / m->tileSize;
		}
	}
}

static void play_draw(Play &p)
{
	Map *m = p.map;
	m->display();
	m->displayActions();
	/* the tile under the cursor */
	if (!p.menuKind && !m->quitDialog && !m->pause)
	{
		int wx = (int)p.cx + camX, wy = (int)p.cy + camY;
		int tx = wx / m->tileSize, ty = wy / m->tileSize;
		bool buildable = m->canBuild(wx, wy) && m->towerAt(wx, wy) == -1;
		bool onTower = m->towerAt(wx, wy) != -1;
		uint32_t col = onTower ? 0xFFE066 : buildable ? 0x66FF66 : 0xFF5050;
		rect_outline(tx * m->tileSize - camX, ty * m->tileSize - camY, m->tileSize, m->tileSize, col, 190);
		/* the pointer */
		int x = (int)p.cx, y = (int)p.cy;
		draw_line(x - 8, y, x + 8, y, 0x000000);
		draw_line(x, y - 8, x, y + 8, 0x000000);
		draw_line(x - 7, y, x + 7, y, 0xFFFFFF);
		draw_line(x, y - 7, x, y + 7, 0xFFFFFF);
	}
	if (p.menuKind)
		draw_radial(p);
	m->drawHud();
	if (m->pause && !m->quitDialog)
		draw_text("PAUSED", SW / 2, SH / 2 - 14, 28, 0xFFFFFF, ALIGN_CENTER, 2);
	if (p.speed2 && !m->pause)
		draw_text("x2", SW - 14, 8, 14, 0xFFE066, ALIGN_RIGHT, 2);
	if (m->quitDialog)
	{
		fill_rect(SW / 2 - 150, SH / 2 - 56, 300, 112, 0x000000, 190);
		rect_outline(SW / 2 - 150, SH / 2 - 56, 300, 112, 0x89F9FF, 160);
		draw_text("Do you really want to quit\nand go back to the menu?", SW / 2, SH / 2 - 44, 12, 0xFFFFFF, ALIGN_CENTER, 1);
		draw_button("NO", SW / 2 - 110, SH / 2 + 12, 70, 26, p.quitSel == 0, 18);
		draw_button("YES", SW / 2 + 40, SH / 2 + 12, 70, 26, p.quitSel == 1, 18);
	}
	else if (!p.menuKind && !m->gameStarted && (int)millis() - p.started < 15000 && !m->pause)
		draw_text("D-Pad: move   A: build / select   B: cancel   X: show build spots   R1: x2 speed   Start: pause   Select: menu",
		          SW / 2, SH - 82, 9, 0xFFFFFF, ALIGN_CENTER, 1, 170);
}

/* ------------------------------------------------------------------ main */
int main()
{
	if (!gfx_init())
		return 1;
	load_image("data/logo_dm.png", logo);
	load_image("data/repair_icon.png", repairIcon);
	load_image("data/update_icon.png", updateIcon);
	load_image("data/sell_icon.png", sellIcon);
	load_maps();
	optShowFps = cfg_num("show_fps", 0) != 0;
	optHQ = cfg_num("hq_graphics", 0) != 0;

	Screen screen = S_MAIN, prevScreen = S_QUIT;
	int mainSel = 0, optSel = 0, afterSel = 0;
	bool afterWon = false;
	Play play;
	uint32_t lastFrame = millis();
	int fpsFrames = 0, fps = 0;
	uint32_t fpsStamp = millis();

	while (screen != S_QUIT)
	{
		uint32_t now = millis();
		float dt = (float)(now - lastFrame);
		lastFrame = now;
		padPrev = padNow;
		padNow = gfx_pad();
		if (held(JK_START) && held(JK_SELECT) && screen != S_GAME)
			break;   /* the quit failsafe on every screen but a match, where Select+Start also quits below */
		if (held(JK_START) && held(JK_SELECT) && play.map && play.map->quitDialog)
			break;

		Screen next = screen;
		switch (screen)
		{
		case S_MAIN: next = screen_main(mainSel); break;
		case S_PLAY: next = screen_play(); break;
		case S_OPTIONS: next = screen_options(optSel); break;
		case S_CREDITS: next = screen_credits(); break;
		case S_AFTER: next = screen_after(afterWon, afterSel); break;
		case S_LOADING:
			gfx_clear(0, 0, 0);
			draw_text("LOADING ...", SW / 2, SH / 2 - 14, 24, 0xFFFFFF, ALIGN_CENTER, 0);
			gfx_present();
			lastMap = mapPicked;
			lastDifficulty = difficulty;
			delete play.map;
			play = Play();
			play.map = new Map("data/maps/" + maps[mapPicked].url, difficulty);
			play.cx = SW / 2;
			play.cy = SH / 2;
			play.started = (int)millis();
			camX = camY = 0;
			if (!play.map->ok)
			{
				printf("could not load map %s\n", maps[mapPicked].url.c_str());
				delete play.map;
				play.map = NULL;
				next = S_PLAY;
			}
			else
			{
				next = S_GAME;
				padPrev = padNow = gfx_pad();
			}
			break;
		case S_GAME:
			play_input(play, dt);
			play_draw(play);
			if (play.map->finished)
			{
				afterWon = play.map->finishedWon;
				afterSel = 0;
				if (play.map->quitDialog && !play.map->finishedWon)
					next = S_MAIN;   /* quit from the pause dialog goes straight back */
				else
					next = S_AFTER;
				delete play.map;
				play.map = NULL;
				grade_set_map("");
			}
			break;
		default: break;
		}
		if (next != screen)
			padPrev = padNow;   /* a press that changed the screen does not also act on the next one */
		screen = next;
		if (optShowFps && screen != S_LOADING)
		{
			char b[48];
			snprintf(b, sizeof b, "%dx%d @ %d fps", SW, SH, fps);
			draw_text(b, SW - 4, 2, 10, 0xFFFFFF, ALIGN_RIGHT, 1);
		}
		fpsFrames++;
		if (millis() - fpsStamp >= 1000)
		{
			fps = fpsFrames;
			fpsFrames = 0;
			fpsStamp = millis();
		}
		gfx_present();
		uint32_t spent = millis() - now;
		if (spent < 25)
			SDL_Delay(25 - spent);
	}
	delete play.map;
	hwdisp_deinit();
	SDL_Quit();
	return 0;
}
