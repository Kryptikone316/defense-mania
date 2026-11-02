/* Defense Mania for the Data Frog SF3500: menus, the pad cursor and the main loop. */
#include "audio.hpp"
#include "game.hpp"
#include "hwdisp.hpp"
#include "joykey.hpp"

#include <SDL.h>
#include <sys/stat.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>

static const float PI_F = 3.14159265f;
static const uint32_t GOLD = 0xFFF8C8;

enum Screen { S_MAIN, S_PLAY, S_OPTIONS, S_CREDITS, S_SCORES, S_LOADING, S_GAME, S_AFTER, S_QUIT };

/* ------------------------------------------------------------------ pad */
static unsigned padNow, padPrev;
static inline bool held(int jk) { return (padNow >> jk) & 1; }
static inline bool pressed(int jk) { return held(jk) && !((padPrev >> jk) & 1); }

/* ------------------------------------------------------------------ menu pieces */
struct MapInfo { std::string name, url, size; Image logo; int perfectBase = 0; };   /* perfectBase: the best possible score at difficulty x1 */
static std::vector<MapInfo> maps;
static Image logo;
static int difficulty = 2;       /* 1 easy, 2 normal, 3 hard */
static int mapPicked = 0;
static int lastMap = 0, lastDifficulty = 2;
static bool playEndless = false, lastEndless = false;   /* the mode picked, and the mode of the match that just ended */

static void draw_button(const char *label, int x, int y, int w, int h, bool active, int size = 20)
{
	uint32_t light = active ? 0x89F9FF : 0xF8DDC2, dark = active ? 0x001807 : 0x251002;
	fill_rect(x, y + h - 1, w, 1, light);
	fill_rect(x, y + h, w, 1, dark);
	draw_text(label, x + w / 2, y + (h - size) / 2 - 2, size, active ? 0x89F9FF : 0xD0D0D0, ALIGN_CENTER, 0);
}

static void draw_hint(const char *s) { draw_text(s, SW / 2, SH - 16, 10, 0xB0B0B0, ALIGN_CENTER, 1); }

/* ------------------------------------------------------------------ saved options and music folders */
static std::string settings_path()
{
	const char *home = getenv("HOME");
	return std::string(home ? home : ".") + "/.defensemania_settings";
}

static void load_settings()
{
	FILE *f = fopen(settings_path().c_str(), "r");
	if (!f)
		return;
	char line[100], key[40];
	int v;
	while (fgets(line, sizeof line, f))
		if (sscanf(line, " %39[a-z_] = %d", key, &v) == 2)
		{
			if (!strcmp(key, "hq")) optHQ = v != 0;
			else if (!strcmp(key, "fps")) optShowFps = v != 0;
			else if (!strcmp(key, "master")) audio_set_master(v);
			else if (!strcmp(key, "music_volume")) music_set_volume(v);
			else if (!strcmp(key, "sfx_volume")) sfx_set_volume(v);
		}
	fclose(f);
}

static void save_settings()
{
	FILE *f = fopen(settings_path().c_str(), "w");
	if (!f)
		return;
	fprintf(f, "hq = %d\nfps = %d\nmaster = %d\nmusic_volume = %d\nsfx_volume = %d\n", optHQ, optShowFps, audio_master(), music_volume(), sfx_volume());
	fclose(f);
}

static bool dir_exists(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }

/* the folder of tracks that goes with a map, Conquest when it has none of its own */
static std::string music_folder_for(const std::string &url, const std::string &name)
{
	std::string id = url;
	if (id.size() > 4 && id.compare(id.size() - 4, 4, ".xml") == 0)
		id.resize(id.size() - 4);
	if (id == "mountains") return "Mountain";
	if (id == "maze") return "The Maze";
	if (id == "woods") return "Dark Woods";
	if (id == "river_side") return "River Side";
	if (dir_exists("music/" + name)) return name;
	return "Conquest";
}


/* ------------------------------------------------------------------ high scores */
/* One player profile: each map keeps its best scores over all difficulties, with the difficulty and the wave reached. */
struct ScoreEntry { int score = 0, difficulty = 2, waves = 0, total = 0; bool won = false; };
static std::map<std::string, std::vector<ScoreEntry> > scoreTables;   /* by map id, best first */
static const int SCORE_ROWS = 8;
static std::map<std::string, int> perfectMask;   /* by map id: bit d-1 is set once difficulty d was played perfectly */

static std::string map_id(const std::string &url) { std::string id = url; if (id.size() > 4 && id.compare(id.size() - 4, 4, ".xml") == 0) id.resize(id.size() - 4); return id; }
static std::string scores_path() { const char *h = getenv("HOME"); return std::string(h ? h : ".") + "/.defensemania_scores"; }

static void load_scores()
{
	FILE *f = fopen(scores_path().c_str(), "r");
	if (!f)
		return;
	char line[200], id[64];
	int diff, score, waves, total, won;
	while (fgets(line, sizeof line, f))
	{
		int mask;
		if (sscanf(line, "perfect %63s %d", id, &mask) == 2)
		{
			perfectMask[id] |= mask;
			continue;
		}
		if (sscanf(line, "%63s %d %d %d %d %d", id, &diff, &score, &waves, &total, &won) == 6)
		{
			ScoreEntry e;
			e.score = score; e.difficulty = diff; e.waves = waves; e.total = total; e.won = won != 0;
			scoreTables[id].push_back(e);
		}
	}
	fclose(f);
}

static void save_scores()
{
	FILE *f = fopen(scores_path().c_str(), "w");
	if (!f)
		return;
	for (std::map<std::string, int>::const_iterator pm = perfectMask.begin(); pm != perfectMask.end(); ++pm)
		fprintf(f, "perfect %s %d\n", pm->first.c_str(), pm->second);
	for (std::map<std::string, std::vector<ScoreEntry> >::const_iterator it = scoreTables.begin(); it != scoreTables.end(); ++it)
		for (size_t i = 0; i < it->second.size(); i++)
		{
			const ScoreEntry &e = it->second[i];
			fprintf(f, "%s %d %d %d %d %d\n", it->first.c_str(), e.difficulty, e.score, e.waves, e.total, e.won);
		}
	fclose(f);
}

/* puts a score in its map's table; returns its place (0-based), or -1 when it did not make the list */
static int add_score(const std::string &mapId, const ScoreEntry &e)
{
	if (e.score <= 0)
		return -1;
	std::vector<ScoreEntry> &t = scoreTables[mapId];
	int r = 0;
	while (r < (int)t.size() && t[r].score >= e.score)
		r++;
	if (r >= SCORE_ROWS)
		return -1;
	t.insert(t.begin() + r, e);
	if ((int)t.size() > SCORE_ROWS)
		t.resize(SCORE_ROWS);
	save_scores();
	return r;
}

/* what the finished match was worth */
struct Result {
	std::string mapId, mapName;
	int difficulty = 2, kills = 0, wavesCleared = 0, totalWaves = 0, lives = 0, money = 0, score = 0;
	bool won = false;
	float mult = 1;
	int winBonus = 0, lifeBonus = 0, waveBonus = 0, perfect = 0, perfectBase = 0;
	int rank = -1;               /* place in the table, -1 when it did not make it */
	Stats stats;
	std::vector<std::string> towerNames;
	bool endless = false;
};

static float diff_mult(int d) { return d == 1 ? 1.0f : d == 2 ? 1.5f : 2.0f; }

static void make_result(Result &r, const Map *m, bool won, const MapInfo &info)
{
	r.endless = m->endless;
	const std::string &name = info.name;
	r.mapName = name;
	r.mapId = m->id;
	r.difficulty = m->difficulty;
	r.kills = m->killPoints;
	r.wavesCleared = m->wavesCleared;
	r.totalWaves = m->endless ? 0 : (int)m->waves->waves.size();
	r.lives = m->lifes;
	r.money = m->money;
	r.stats = m->stats;
	r.towerNames.clear();
	for (size_t i = 0; i < m->towerTypes.size(); i++)
		r.towerNames.push_back(m->towerTypes[i].name);
	r.won = won;
	r.mult = m->scoreMult();
	r.waveBonus = r.wavesCleared * 100;
	r.winBonus = won && !m->endless ? 1000 : 0;
	r.lifeBonus = won && !m->endless ? r.lives * 100 : 0;
	r.perfectBase = m->endless ? 0 : info.perfectBase;
	r.perfect = (int)(r.perfectBase * r.mult);
	r.score = (int)((r.kills + r.waveBonus + r.winBonus + r.lifeBonus) * r.mult);
	ScoreEntry e;
	e.score = r.score; e.difficulty = r.difficulty; e.waves = r.wavesCleared; e.total = r.totalWaves; e.won = won;
	r.rank = add_score(m->endless ? "endless:" + r.mapId : r.mapId, e);
	if (!m->endless && r.perfect > 0 && r.score >= r.perfect)   /* remembered for good, for the endless unlock */
	{
		perfectMask[r.mapId] |= 1 << (r.difficulty - 1);
		save_scores();
	}
}


/* Endless mode of a map opens once all three difficulties of it have been played perfectly. */
static int perfect_bits(const MapInfo &m)
{
	std::string id = map_id(m.url);
	int bits = perfectMask.count(id) ? perfectMask[id] : 0;
	const std::vector<ScoreEntry> &t = scoreTables[id];
	for (size_t i = 0; i < t.size(); i++)
		if (t[i].won && m.perfectBase > 0 && t[i].score >= (int)(m.perfectBase * diff_mult(t[i].difficulty)))
			bits |= 1 << (t[i].difficulty - 1);
	return bits;
}
static int perfect_count(const MapInfo &m) { int b = perfect_bits(m), n = 0; for (int i = 0; i < 3; i++) n += (b >> i) & 1; return n; }
static bool endless_unlocked(const MapInfo &m) { return cfg_num("endless_unlocked", 0) != 0 || perfect_bits(m) == 7; }

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
		/* every enemy beaten, every wave cleared, the match won and no life lost */
		XNode waves, tree;
		if (xml_load("data/maps/" + m.url + "/waves.xml", waves) && xml_load("data/maps/" + m.url + "/maptree.xml", tree))
		{
			std::vector<const XNode *> ws = waves.list("waves/wave");
			int kills = 0;
			for (size_t w = 0; w < ws.size(); w++)
				for (size_t k = 0; k < ws[w]->kids.size(); k++)
					if (ws[w]->kids[k].name == "enemy")
						kills += ws[w]->kids[k].num("count") * ws[w]->kids[k].num("money") * 5;
			const XNode *game = tree.child("game");
			m.perfectBase = kills + (int)ws.size() * 100 + 1000 + (game ? game->num("lifes") * 100 : 0);
		}
		maps.push_back(m);
	}
}

static Screen screen_main(int &sel)
{
	static const char *labels[5] = { "PLAY", "SCORES", "CREDITS", "OPTIONS", "EXIT" };
	gfx_clear(0, 0, 0);
	if (logo.valid())
	{
		int w = logo.w * 62 / 100, h = logo.h * 62 / 100;
		draw_image_scaled(logo, (SW - w) / 2, 8, w, h);
	}
	int bw = 112, gap = (SW - bw * 5) / 6;
	for (int i = 0; i < 5; i++)
		draw_button(labels[i], gap + i * (bw + gap), SH - 62, bw, 26, i == sel, 20);
	draw_hint("D-Pad: choose    A: select");
	if (pressed(JK_LEFT)) sel = (sel + 4) % 5;
	if (pressed(JK_RIGHT)) sel = (sel + 1) % 5;
	if (pressed(JK_A))
	{
		if (sel == 0) return S_PLAY;
		if (sel == 1) return S_SCORES;
		if (sel == 2) return S_CREDITS;
		if (sel == 3) return S_OPTIONS;
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
		int y = 96 + (int)i * 35;
		bool on = (int)i == mapPicked;
		draw_text(maps[i].name, listX + 8, y + 2, 18, on ? 0x89F9FF : 0xD0D0D0, ALIGN_LEFT, 0);
		draw_text(maps[i].size, listX + 292, y + 6, 11, on ? 0x89F9FF : 0x909090, ALIGN_RIGHT, 1);
		if (on)
			rect_outline(listX - 4, y - 4, 308, 31, 0x89F9FF, 150);
	}
	bool unlocked = mapPicked >= 0 && mapPicked < (int)maps.size() && endless_unlocked(maps[mapPicked]);
	if (!unlocked)
		playEndless = false;
	if (mapPicked >= 0 && mapPicked < (int)maps.size() && maps[mapPicked].logo.valid())
		draw_image(maps[mapPicked].logo, SW - 70 - maps[mapPicked].logo.w, 98);
	draw_text("MODE", 410, 166, 11, 0x909090, ALIGN_LEFT, 1);
	draw_text(playEndless ? "ENDLESS" : "CAMPAIGN", 410, 180, 20, playEndless ? 0xFFE066 : 0xFFFFFF, ALIGN_LEFT, 0);
	if (unlocked)
		draw_text("X: switch mode", 410, 210, 12, 0xB0B0B0, ALIGN_LEFT, 1);
	else if (mapPicked >= 0 && mapPicked < (int)maps.size())
	{
		char b[140];
		snprintf(b, sizeof b, "Endless unlocks with a perfect\nscore on all three difficulties\nof this map  (%d / 3)", perfect_count(maps[mapPicked]));
		draw_text(b, 410, 210, 12, 0xB0B0B0, ALIGN_LEFT, 1);
	}
	draw_button("BACK", 40, SH - 54, 110, 26, false, 20);
	draw_button("PLAY", SW - 150, SH - 54, 110, 26, true, 20);
	draw_hint("Up/Down: map    Left/Right: difficulty    X: mode    A: play    B: back");
	if (pressed(JK_X) && unlocked) playEndless = !playEndless;
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
	const char *name[5] = { "VOLUME", "MUSIC", "EFFECTS", "HQ GRAPHICS", "SHOW FPS" };
	const char *help[5] = {
		"Left/Right: overall volume, music and\neffects together.",
		dir_exists("music") ? "Left/Right: music volume. 0 turns it off.\nA random track of the map's own folder."
		                    : "Left/Right: music volume. No music pack is\ninstalled; its music folder goes next to the game.",
		"Left/Right: volume of the sound effects\n(shots, building, the wave horn ...).",
		"A: adds a light overlay on the map, which costs\nperformance. Takes effect on the next match.",
		"A: shows the current frame rate." };
	char v0[16], v1[16], v2[16];
	snprintf(v0, sizeof v0, "%d%%", audio_master());
	snprintf(v1, sizeof v1, "%d%%", music_volume());
	snprintf(v2, sizeof v2, "%d%%", sfx_volume());
	std::string state[5] = { v0, v1, v2, optHQ ? "ON" : "OFF", optShowFps ? "ON" : "OFF" };
	bool on[5] = { audio_master() > 0, music_volume() > 0, sfx_volume() > 0, optHQ, optShowFps };
	for (int i = 0; i < 5; i++)
	{
		int y = 14 + i * 53;
		draw_button(name[i], 40, y, 190, 26, i == sel, 20);
		draw_text(state[i], 250, y + 3, 16, on[i] ? 0x89F9FF : 0x909090, ALIGN_LEFT, 0);
		draw_text(help[i], 330, y + 1, 11, 0xE6E6E6, ALIGN_LEFT, 1);
		if (i == sel)
			rect_outline(34, y - 6, SW - 68, 40, 0x89F9FF, 120);
	}
	draw_button("BACK", 40, SH - 54, 110, 26, false, 20);
	draw_hint("Up/Down: choose    Left/Right or A: change    B: back");
	if (pressed(JK_UP) && sel > 0) sel--;
	if (pressed(JK_DOWN) && sel < 4) sel++;
	int step = (pressed(JK_RIGHT) ? 10 : 0) - (pressed(JK_LEFT) ? 10 : 0);
	if (sel == 0 && step) audio_set_master(audio_master() + step);
	if (sel == 1 && step) music_set_volume(music_volume() + step);
	if (sel == 2 && step) { sfx_set_volume(sfx_volume() + step); sfx_play(SFX_BUILD); }
	if (pressed(JK_A))
	{
		if (sel == 3) optHQ = !optHQ;
		else if (sel == 4) optShowFps = !optShowFps;
		else if (sel == 2) sfx_play(SFX_BUILD);
	}
	if (pressed(JK_B))
	{
		save_settings();
		return S_MAIN;
	}
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

static void draw_table(const std::string &mapId, int x, int y, int highlight, int perfectBase)
{
	static const char *dif[3] = { "EASY", "NORMAL", "HARD" };
	const std::vector<ScoreEntry> &t = scoreTables[mapId];
	draw_text("DIFFICULTY", x + 40, y, 12, 0x909090, ALIGN_LEFT, 1);
	draw_text("WAVE", x + 190, y, 12, 0x909090, ALIGN_RIGHT, 1);
	draw_text("SCORE", x + 310, y, 12, 0x909090, ALIGN_RIGHT, 1);
	fill_rect(x, y + 17, 320, 1, 0xF8DDC2);
	for (int i = 0; i < SCORE_ROWS; i++)
	{
		int ry = y + 22 + i * 26;
		bool hl = i == highlight;
		uint32_t col = hl ? 0xFFD400 : i < (int)t.size() ? 0xFFFFFF : 0x606060;
		if (hl)
			fill_rect(x - 4, ry - 3, 328, 24, 0xFFD400, 45);
		char b[24];
		snprintf(b, sizeof b, "%d.", i + 1);
		draw_text(b, x + 4, ry, 16, col, ALIGN_LEFT, 0);
		if (i < (int)t.size())
		{
			draw_text(dif[t[i].difficulty - 1], x + 40, ry, 16, col, ALIGN_LEFT, 0);
			int reached = t[i].won ? t[i].total : (t[i].waves + 1 > t[i].total ? t[i].total : t[i].waves + 1);   /* a win shows 10 / 10 */
			if (t[i].total == 0)   /* endless: no end to count to */
				snprintf(b, sizeof b, "%d", t[i].waves + 1);
			else
				snprintf(b, sizeof b, "%d / %d", reached, t[i].total);
			draw_text(b, x + 190, ry, 16, col, ALIGN_RIGHT, 0);
			snprintf(b, sizeof b, "%d", t[i].score);
			draw_text(b, x + 310, ry, 16, col, ALIGN_RIGHT, 0);
			if (perfectBase > 0 && t[i].score >= (int)(perfectBase * diff_mult(t[i].difficulty)))
				draw_text("PERFECT", x + 248, ry + 3, 10, hl ? 0xFFFFFF : 0xFFD400, ALIGN_RIGHT, 2);
		}
		else
			draw_text("---", x + 40, ry, 16, col, ALIGN_LEFT, 0);
	}
}

static int scoresMap = 0;
static bool scoresEndless = false;

static Screen screen_scores()
{
	gfx_clear(0, 0, 0);
	draw_text(scoresEndless ? "HIGH SCORES  -  ENDLESS" : "HIGH SCORES", SW / 2, 12, 24, scoresEndless ? 0xFFE066 : 0xFFFFFF, ALIGN_CENTER, 0);
	if (maps.empty())
		return S_MAIN;
	draw_text(std::string("<  ") + maps[scoresMap].name + "  >", SW / 2, 48, 18, 0x89F9FF, ALIGN_CENTER, 0);
	int pb = maps[scoresMap].perfectBase;
	char pf[110];
	if (scoresEndless)
		snprintf(pf, sizeof pf, "Best wave reached on this map. The waves never stop.");
	else
		snprintf(pf, sizeof pf, "Perfect score:   Easy %d   Normal %d   Hard %d     (%d / 3 played perfectly)", (int)(pb * 1.0f), (int)(pb * 1.5f), (int)(pb * 2.0f), perfect_count(maps[scoresMap]));
	draw_text(pf, SW / 2, 72, 12, 0xFFE066, ALIGN_CENTER, 1);
	std::string key = map_id(maps[scoresMap].url);
	draw_table(scoresEndless ? "endless:" + key : key, (SW - 320) / 2, 98, -1, scoresEndless ? 0 : pb);
	draw_hint("Left/Right: map    X: campaign / endless    B: back");
	if ((pressed(JK_LEFT) || pressed(JK_UP)) && scoresMap > 0) scoresMap--;
	if ((pressed(JK_RIGHT) || pressed(JK_DOWN)) && scoresMap + 1 < (int)maps.size()) scoresMap++;
	if (pressed(JK_X) || pressed(JK_Y)) scoresEndless = !scoresEndless;
	if (pressed(JK_B) || pressed(JK_START)) return S_MAIN;
	return S_SCORES;
}

static void draw_stats(const Result &r)
{
	const Stats &st = r.stats;
	char b[64];
	draw_text(r.endless ? "RUN OVER" : r.won ? "VICTORY!" : "DEFEATED", 24, 10, 30, r.endless || r.won ? 0xFFE066 : 0xFF7070, ALIGN_LEFT, 0);
	draw_text("MATCH STATS", SW - 24, 20, 18, 0xFFFFFF, ALIGN_RIGHT, 0);
	struct Col { int x; const char *head; } cols[3] = { { 24, "GOLD" }, { 232, "TOWERS" }, { 440, "ENEMIES" } };
	for (int i = 0; i < 3; i++)
	{
		draw_text(cols[i].head, cols[i].x, 62, 14, 0x89F9FF, ALIGN_LEFT, 0);
		fill_rect(cols[i].x, 82, 180, 1, 0xF8DDC2);
	}
	int y = 92, dy = 20;
	auto row = [&](int x, const char *label, const std::string &val) {
		draw_text(label, x, y, 12, 0xE6E6E6, ALIGN_LEFT, 1);
		draw_text(val, x + 180, y, 12, 0xFFFFFF, ALIGN_RIGHT, 2);
	};
	auto num = [&](int v) { snprintf(b, sizeof b, "%d", v); return std::string(b); };
	/* gold */
	int yy = y;
	row(24, "Earned from enemies", num(st.goldEarned)); y += dy;
	row(24, "Spent on towers", num(st.goldSpent)); y += dy;
	row(24, "Gained selling", num(st.goldSold)); y += dy;
	row(24, "Left over", num(r.money)); y += dy;
	/* towers */
	y = yy;
	int total = 0;
	for (size_t i = 0; i < r.towerNames.size() && i < 8; i++)
	{
		row(232, r.towerNames[i].c_str(), num(st.built[i]));
		total += st.built[i];
		y += dy;
	}
	row(232, "Upgrades", num(st.upgrades)); y += dy;
	row(232, "Repairs", num(st.repairs)); y += dy;
	row(232, "Sold", num(st.sold)); y += dy;
	row(232, "Lost in battle", num(st.towersLost)); y += dy;
	/* enemies */
	y = yy;
	row(440, "Defeated", num(st.enemiesDefeated)); y += dy;
	row(440, "Reached the end", num(st.enemiesLeaked)); y += dy;
	row(440, "Shots fired", num(st.shots)); y += dy;
	int sec = st.playMs / 1000;
	snprintf(b, sizeof b, "%d:%02d", sec / 60, sec % 60);
	row(440, "Time played", b); y += dy;
	row(440, "Lives left", num(r.lives)); y += dy;
	snprintf(b, sizeof b, total == 1 ? "%d tower built" : "%d towers built", total);
	draw_text(b, SW / 2, SH - 100, 12, 0xB0B0B0, ALIGN_CENTER, 1);
}

static int afterView = 0;

static Screen screen_after(Result &r, int &sel)
{
	gfx_clear(0, 0, 0);
	if (pressed(JK_UP) || pressed(JK_DOWN) || pressed(JK_X) || pressed(JK_Y)) afterView ^= 1;
	if (afterView == 1)
	{
		draw_stats(r);
		static const char *labels2[3] = { "PLAY AGAIN", "BACK TO MENU", "EXIT GAME" };
		int bw2 = 160, gap2 = (SW - bw2 * 3) / 4;
		for (int i = 0; i < 3; i++)
			draw_button(labels2[i], gap2 + i * (bw2 + gap2), SH - 62, bw2, 26, i == sel, 18);
		draw_hint("Up/Down: score    Left/Right: choose    A: select");
		if (pressed(JK_LEFT)) sel = (sel + 2) % 3;
		if (pressed(JK_RIGHT)) sel = (sel + 1) % 3;
		if (pressed(JK_A))
		{
			afterView = 0;
			if (sel == 0) { mapPicked = lastMap; difficulty = lastDifficulty; playEndless = lastEndless; return S_LOADING; }
			if (sel == 1) return S_PLAY;
			return S_QUIT;
		}
		return S_AFTER;
	}
	draw_text(r.endless ? "RUN OVER" : r.won ? "VICTORY!" : "DEFEATED", 24, 10, 30, r.endless ? 0xFFE066 : r.won ? 0xFFE066 : 0xFF7070, ALIGN_LEFT, 0);
	draw_text(r.mapName + (r.endless ? "  -  Endless" : "") + (r.difficulty == 1 ? "  -  Easy" : r.difficulty == 2 ? "  -  Normal" : "  -  Hard"), 24, 48, 12, 0xB0B0B0, ALIGN_LEFT, 1);
	char b[64];
	int y = 72;
	struct Line { const char *label; int value; } lines[4] = {
		{ "Enemies defeated", r.kills }, { "Waves cleared", r.waveBonus }, { "Victory bonus", r.winBonus },
		{ "Lives left", r.lifeBonus } };
	for (int i = 0; i < 4; i++)
	{
		bool skip = i >= 2 && (!r.won || r.endless);
		draw_text(lines[i].label, 24, y, 13, skip ? 0x505050 : 0xE6E6E6, ALIGN_LEFT, 1);
		snprintf(b, sizeof b, "%d", lines[i].value);
		draw_text(b, 220, y, 13, skip ? 0x505050 : 0xE6E6E6, ALIGN_RIGHT, 1);
		y += 19;
	}
	snprintf(b, sizeof b, "x %.1f", r.mult);
	draw_text("Difficulty", 24, y, 13, 0xE6E6E6, ALIGN_LEFT, 1);
	draw_text(b, 220, y, 13, 0xE6E6E6, ALIGN_RIGHT, 1);
	y += 24;
	fill_rect(24, y - 4, 196, 1, 0xF8DDC2);
	draw_text("TOTAL", 24, y + 2, 18, 0xFFE066, ALIGN_LEFT, 2);
	snprintf(b, sizeof b, "%d", r.score);
	draw_text(b, 220, y + 2, 18, 0xFFE066, ALIGN_RIGHT, 2);
	if (r.endless)
	{
		snprintf(b, sizeof b, "You reached wave %d", r.wavesCleared + 1);
		draw_text(b, 24, y + 34, 15, 0xFFE066, ALIGN_LEFT, 1);
		if (r.rank >= 0)
			draw_text(r.rank == 0 ? "NEW BEST RUN!" : "NEW HIGH SCORE!", 24, y + 56, 16, 0xFFD400, ALIGN_LEFT, 0);
	}
	else if (r.perfect > 0)
	{
		if (r.score >= r.perfect)
			draw_text("PERFECT SCORE!", 24, y + 34, 18, 0xFFD400, ALIGN_LEFT, 0);
		else
		{
			snprintf(b, sizeof b, "Perfect score: %d  (%d%%)", r.perfect, r.score * 100 / r.perfect);
			draw_text(b, 24, y + 34, 13, 0xFFE066, ALIGN_LEFT, 1);
			if (r.rank >= 0)
				draw_text(r.rank == 0 ? "NEW BEST SCORE!" : "NEW HIGH SCORE!", 24, y + 54, 16, 0xFFD400, ALIGN_LEFT, 0);
		}
	}

	draw_table(r.endless ? "endless:" + r.mapId : r.mapId, 300, 14, r.rank, r.perfectBase);

	static const char *labels[3] = { "PLAY AGAIN", "BACK TO MENU", "EXIT GAME" };
	int bw = 160, gap = (SW - bw * 3) / 4;
	for (int i = 0; i < 3; i++)
		draw_button(labels[i], gap + i * (bw + gap), SH - 62, bw, 26, i == sel, 18);
	draw_hint("Up/Down: match stats    Left/Right: choose    A: select");
	if (pressed(JK_LEFT)) sel = (sel + 2) % 3;
	if (pressed(JK_RIGHT)) sel = (sel + 1) % 3;
	if (pressed(JK_A))
	{
		afterView = 0;
		if (sel == 0) { mapPicked = lastMap; difficulty = lastDifficulty; playEndless = lastEndless; return S_LOADING; }
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
	int w = text_width(text, 11, 2) + 16, h = text_lineheight(11, 2) * 2 + 10;
	int x = SW - 10 - w, y = SH - 64 - h;
	fill_rect(x, y, w, h, 0x000000, 200);
	rect_outline(x, y, w, h, 0x89F9FF, 120);
	draw_text(text, x + 8, y + 5, 11, 0xFFFFFF, ALIGN_LEFT, 2);
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
		{
			int pulse = 120 + (int)(80 * (0.5f + 0.5f * sinf(millis() / 120.0f)));
			fill_circle(ix, iy, iconSize / 2 + 6.0f, 0xFFD400, pulse);
			for (int k = 0; k < 3; k++)
				circle_outline(ix, iy, iconSize / 2 + 6.0f + k, 0xFFD400, 255);
			circle_outline(ix, iy, iconSize / 2 + 9.0f, 0x000000, 200);
		}
		draw_image_center(*img, ix, iy, 0, 0, -1, -1, dim ? 200 : 255, dim ? 0x969696 : 0xFFFFFF);
		char b[24];
		snprintf(b, sizeof b, "%d $", cost);
		fill_rect((int)(ix - iconSize * 0.5f), (int)(iy + iconSize / 2 - 3), (int)iconSize, 13, 0x000000, 200);
		draw_text(b, (int)ix, (int)(iy + iconSize / 2 - 3), 10, 0xFFFFFF, ALIGN_CENTER, 2);
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
	if (m->gameStarted && !m->pause && !m->defeated && !m->quitDialog)
		m->stats.playMs += (int)dtMs;
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
		draw_text("x2", SW - 70, 19, 14, 0xFFE066, ALIGN_RIGHT, 2, 255, 0x000000);
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
		          SW / 2, SH - 84, 10, 0xFFFFFF, ALIGN_CENTER, 2, 255, 0x000000);
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
	audio_init();
	load_settings();
	load_scores();
	music_play_folder("Conquest");

	Screen screen = S_MAIN;
	int mainSel = 0, optSel = 0, afterSel = 0;
	Result result;
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

		if (screen != S_GAME && screen != S_LOADING)
		{
			if (pressed(JK_UP) || pressed(JK_DOWN) || pressed(JK_LEFT) || pressed(JK_RIGHT)) sfx_play(SFX_MOVE);
			if (pressed(JK_A)) sfx_play(SFX_SELECT);
		}
		Screen next = screen;
		switch (screen)
		{
		case S_MAIN: next = screen_main(mainSel); break;
		case S_PLAY: next = screen_play(); break;
		case S_OPTIONS: next = screen_options(optSel); break;
		case S_CREDITS: next = screen_credits(); break;
		case S_AFTER: next = screen_after(result, afterSel); break;
		case S_SCORES: next = screen_scores(); break;
		case S_LOADING:
			gfx_clear(0, 0, 0);
			draw_text("LOADING ...", SW / 2, SH / 2 - 14, 24, 0xFFFFFF, ALIGN_CENTER, 0);
			gfx_present();
			lastMap = mapPicked;
			lastDifficulty = difficulty;
			delete play.map;
			play = Play();
			lastEndless = playEndless;
			play.map = new Map("data/maps/" + maps[mapPicked].url, difficulty, playEndless);
			play.cx = SW / 2;
			play.cy = SH / 2;
			if (getenv("DM_CURSOR"))   /* test hook: start the cursor at x,y on the screen */
				sscanf(getenv("DM_CURSOR"), "%f,%f", &play.cx, &play.cy);
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
				music_play_folder(music_folder_for(maps[mapPicked].url, maps[mapPicked].name));
			}
			break;
		case S_GAME:
			if (getenv("DM_FAKE_RESULT") && (int)millis() - play.started > (getenv("DM_FAKE_DELAY") ? atoi(getenv("DM_FAKE_DELAY")) : 1500) && !play.map->finished)
			{   /* test hook: end the match at once with some score */
				play.map->killPoints = 640;
				play.map->wavesCleared = atoi(getenv("DM_FAKE_RESULT"));
				play.map->finished = true;
				play.map->finishedWon = false;
			}
			play_input(play, dt);
			play_draw(play);
			if (play.map->finished)
			{
				afterSel = 0;
				if (play.map->quitDialog && !play.map->finishedWon && !play.map->endless)
					next = S_MAIN;   /* quit from the pause dialog goes straight back */
				else
				{
					make_result(result, play.map, play.map->finishedWon, maps[lastMap]);
					next = S_AFTER;
				}
				delete play.map;
				play.map = NULL;
				grade_set_map("");
				music_play_folder("Conquest");
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
			draw_text(b, SW - 70, 3, 11, 0xFFFFFF, ALIGN_RIGHT, 2, 255, 0x000000);   /* left of TreeFrogUI's battery icon */
		}
		music_update();
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
	save_settings();
	audio_shutdown();
	hwdisp_deinit();
	SDL_Quit();
	return 0;
}
