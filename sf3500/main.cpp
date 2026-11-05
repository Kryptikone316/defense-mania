/* Defense Mania for the Data Frog SF3500: menus, the pad cursor and the main loop. */
#include "audio.hpp"
#include "game.hpp"
#include "hwdisp.hpp"
#include "joykey.hpp"
#include "slotcheck.hpp"

#include <SDL.h>
#include <sys/stat.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#include <ucontext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>

static const float PI_F = 3.14159265f;
static const uint32_t GOLD = 0xFFF8C8;

enum Screen { S_MAIN, S_PLAY, S_HELP, S_OPTIONS, S_CREDITS, S_SCORES, S_LOADING, S_GAME, S_AFTER, S_QUIT };

/* ------------------------------------------------------------------ pad */
static unsigned padNow, padPrev;
static inline bool held(int jk) { return (padNow >> jk) & 1; }
static inline bool pressed(int jk) { return held(jk) && !((padPrev >> jk) & 1); }

/* ------------------------------------------------------------------ menu pieces */
struct MapInfo { std::string name, url, size; Image logo; int perfectBase = 0; };   /* perfectBase: the best possible score at difficulty x1 */
static std::vector<MapInfo> maps;
static Image logo;
static int difficulty = 1;       /* 1 easy, 2 normal, 3 hard */
static int mapPicked = 0;
static int lastMap = 0, lastDifficulty = 1;
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
static std::map<std::string, int> wonMask;       /* by map id: bit d-1 is set once difficulty d was won; each win opens the next one */

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
		if (sscanf(line, "won %63s %d", id, &mask) == 2)
		{
			wonMask[id] |= mask;
			continue;
		}
		if (sscanf(line, "%63s %d %d %d %d %d", id, &diff, &score, &waves, &total, &won) == 6)
		{
			ScoreEntry e;
			e.score = score; e.difficulty = diff; e.waves = waves; e.total = total; e.won = won != 0;
			scoreTables[id].push_back(e);
			if (e.won && strncmp(id, "endless:", 8) != 0 && diff >= 1 && diff <= 3)   /* scores saved before the wins were counted */
				wonMask[id] |= 1 << (diff - 1);
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
	for (std::map<std::string, int>::const_iterator wm = wonMask.begin(); wm != wonMask.end(); ++wm)
		fprintf(f, "won %s %d\n", wm->first.c_str(), wm->second);
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
	std::vector<std::string> towerNames, trapNames;
	bool endless = false;
	int newUnlock = 0;           /* a difficulty this win opened, 0 for none */
};

static float diff_mult(int d) { return d == 1 ? 1.0f : d == 2 ? 1.5f : 2.0f; }

/* Every map starts on Easy; winning a map on one difficulty opens the next one on that map (Easy, Normal, Hard). */
static int diff_unlocked_id(const std::string &id)
{
	if (cfg_num("unlock_all_difficulties", 0) != 0)
		return 3;
	int bits = (wonMask.count(id) ? wonMask[id] : 0) | (perfectMask.count(id) ? perfectMask[id] : 0), top = 0;
	for (int d = 1; d <= 3; d++)
		if (bits & (1 << (d - 1)))
			top = d;
	return top >= 3 ? 3 : top + 1;
}

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
	r.trapNames.clear();
	for (size_t i = 0; i < m->trapTypes.size(); i++)
		r.trapNames.push_back(m->trapTypes[i].name);
	r.won = won;
	r.mult = m->scoreMult();
	r.waveBonus = r.wavesCleared * 100;
	r.winBonus = won && !m->endless ? 1000 : 0;
	r.lifeBonus = won && !m->endless ? (r.lives < m->baseLifes ? r.lives : m->baseLifes) * 100 : 0;   /* the Bastion's extra lives do not add to the score */
	r.perfectBase = m->endless ? 0 : info.perfectBase;
	r.perfect = (int)(r.perfectBase * r.mult);
	r.score = (int)((r.kills + r.waveBonus + r.winBonus + r.lifeBonus) * r.mult);
	ScoreEntry e;
	e.score = r.score; e.difficulty = r.difficulty; e.waves = r.wavesCleared; e.total = r.totalWaves; e.won = won;
	if (won && !m->endless)
	{
		int before = diff_unlocked_id(r.mapId);
		wonMask[r.mapId] |= 1 << (r.difficulty - 1);
		int after = diff_unlocked_id(r.mapId);
		r.newUnlock = after > before ? after : 0;
	}
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
	static const char *labels[6] = { "PLAY", "HOW TO PLAY", "SCORES", "CREDITS", "OPTIONS", "EXIT" };
	static const int widths[6] = { 80, 150, 92, 92, 92, 70 };
	gfx_clear(0, 0, 0);
	if (logo.valid())
	{
		int w = logo.w * 62 / 100, h = logo.h * 62 / 100;
		draw_image_scaled(logo, (SW - w) / 2, 8, w, h);
	}
	int total = 0;
	for (int i = 0; i < 6; i++) total += widths[i];
	int gap = (SW - total) / 7, x = gap;
	for (int i = 0; i < 6; i++)
	{
		draw_button(labels[i], x, SH - 62, widths[i], 26, i == sel, 16);
		x += widths[i] + gap;
	}
	draw_hint("D-Pad: choose    A: select");
	if (pressed(JK_LEFT)) sel = (sel + 5) % 6;
	if (pressed(JK_RIGHT)) sel = (sel + 1) % 6;
	if (pressed(JK_A))
	{
		if (sel == 0) return S_PLAY;
		if (sel == 1) return S_HELP;
		if (sel == 2) return S_SCORES;
		if (sel == 3) return S_CREDITS;
		if (sel == 4) return S_OPTIONS;
		return S_QUIT;
	}
	return S_MAIN;
}

/* ------------------------------------------------------------------ how to play: a few pages, Left/Right turn them */
static Screen screen_help(int &page)
{
	struct Page { const char *head, *text; };
	static const Page pages[6] = {
		{ "THE GAME",
		  "Creatures march along the path to your exit. Every one that gets through costs a life; lose\n"
		  "them all and the match is lost. Stop them with towers, traps and the Bastion, and win by\n"
		  "clearing every wave. Endless mode never ends: see how far you get.\n\n"
		  "Every creature you defeat pays gold, a little more on the harder levels and in later waves.\n"
		  "Spend it on towers, upgrades, repairs, traps and research.\n\n"
		  "CONTROLS\n"
		  "D-Pad: move the cursor (near the edge it scrolls the map)     A: choose, build, open the menu\n"
		  "B: close a menu     X: show where towers can go     R1 or R2: double speed\n"
		  "Start: pause     Select: leave the match" },
		{ "TOWERS",
		  "Press A on a green spot to build. Press A on a tower to upgrade, repair or sell it.\n\n"
		  "Arrow: balanced and cheap; hits ground and air.\n"
		  "Ballista: long range; each shot on the same target hits harder. Hits air too.\n"
		  "Catapult and Canon: splash damage, good against swarms. They cannot hit flyers.\n"
		  "Freeze: slows creatures, and slowed creatures take extra damage from every tower.\n"
		  "Resupply Depot: no weapon. The towers around it fire faster and mend, and creatures that\n"
		  "die inside its area pay extra gold. Soldiers standing in its area mend too, more slowly.\n\n"
		  "Creatures hit towers too. A damaged tower can be repaired; a destroyed one is gone." },
		{ "TRAPS",
		  "Press A on the path itself, away from the green spots, to set a trap. Each kind has a\n"
		  "limit on how many you can have at once.\n\n"
		  "Spikes: hurt each creature that crosses them, until their charges are used up.\n"
		  "Glue: slows the creatures that walk through it, then dries up.\n"
		  "Boulder: blocks the path until the creatures smash it. Watch out: ogres and trolls will\n"
		  "lift a boulder and throw it at a tower nearby.\n\n"
		  "Traps only work on creatures that walk. Flyers pass over them." },
		{ "THE BASTION",
		  "The keep at your exit. Put the cursor on it and press A. The game stops while its menu is\n"
		  "open and carries on the moment you close it.\n\n"
		  "RESEARCH: pay gold to improve a tower, a trap or the keep itself. One project at a time;\n"
		  "once started it cannot be stopped, and it finishes after the number of waves shown.\n"
		  "Nothing is kept from match to match: every match starts fresh.\n\n"
		  "ACTIONS: Volley, Rally, Mend and Frost Horn, used from the menu when a wave is on. Each\n"
		  "needs research, and research shortens the wait before the next use.\n\n"
		  "FORTIFY: more lives for the match." },
		{ "THE GARRISON",
		  "Research can also train soldiers, sent out from the Actions menu. Each kind is good against\n"
		  "certain creatures and weak against the rest.\n\n"
		  "Vermin Hunters: spiders, wargs and gnolls.     Giant Slayers: orcs, ogres and trolls; they\n"
		  "ignore armour.     Skyguard: slingers; the only soldiers who can reach flyers.\n"
		  "Boulder Rollers: push a boulder up the path that hits every ground creature it touches and\n"
		  "wears down as it goes.\n\n"
		  "Fighters guard the last stretch of the path before the keep: they are your last line, for the\n"
		  "creatures that get past the towers. They are brave but frail and do not stay long. They\n"
		  "help your towers; they do not replace them." },
		{ "DOCTRINES AND TIPS",
		  "Every research tier you finish counts towards its path: Towers, Traps, Garrison or Command.\n"
		  "At 3, 6 and 9 tiers in one path you earn a doctrine bonus for everything in that path.\n"
		  "You may spread your research as you like; focusing just pays more.\n\n"
		  "TIPS\n"
		  "Look at what is coming: the icons at the bottom show the next wave.\n"
		  "Mix your towers: swarms need splash, big creatures need heavy hitters, flyers need arrows.\n"
		  "Do not spend every coin at once: damage to towers needs gold for repairs.\n"
		  "In Endless mode a supply drop of gold arrives every few waves." }
	};
	gfx_clear(0, 0, 0);
	draw_text("HOW TO PLAY", 24, 10, 28, 0xFFE066, ALIGN_LEFT, 0);
	char b[16];
	snprintf(b, sizeof b, "%d / 6", page + 1);
	draw_text(b, SW - 24, 18, 16, 0xB0B0B0, ALIGN_RIGHT, 0);
	draw_text(pages[page].head, 24, 50, 18, 0x89F9FF, ALIGN_LEFT, 0);
	fill_rect(24, 74, SW - 48, 1, 0xF8DDC2);
	draw_text(pages[page].text, 24, 84, 12, 0xE6E6E6, ALIGN_LEFT, 1);
	draw_hint("Left/Right: page     B: back");
	if (pressed(JK_LEFT)) page = (page + 5) % 6;
	if (pressed(JK_RIGHT) || pressed(JK_A)) page = (page + 1) % 6;
	if (pressed(JK_B) || pressed(JK_START)) { page = 0; return S_MAIN; }
	return S_HELP;
}

static int mapScroll = 0;           /* first map shown in the list */
static uint32_t lockedMsgUntil = 0;

static Screen screen_play()
{
	gfx_clear(0, 0, 0);
	static const char *dif[3] = { "EASY", "NORMAL", "HARD" };
	bool have = mapPicked >= 0 && mapPicked < (int)maps.size();
	int open = have ? diff_unlocked_id(map_id(maps[mapPicked].url)) : 1;
	if (difficulty > open) difficulty = open;
	int bw = 110, gap = (SW - bw * 3) / 4;
	for (int i = 0; i < 3; i++)
	{
		bool locked = i + 1 > open;
		draw_button(locked ? "LOCKED" : dif[i], gap + i * (bw + gap), 22, bw, 26, difficulty == i + 1, 20);
		if (locked)   /* greyed out */
			fill_rect(gap + i * (bw + gap), 20, bw, 30, 0x000000, 150);
	}
	draw_text("L1", gap - 6, 28, 12, 0x89F9FF, ALIGN_RIGHT, 2);
	draw_text("R1", SW - gap + 6, 28, 12, 0x89F9FF, ALIGN_LEFT, 2);
	if (millis() < lockedMsgUntil && difficulty < 3)
		draw_text(difficulty == 1 ? "Win this map on Easy to unlock Normal" : "Win this map on Normal to unlock Hard", SW / 2, 52, 11, 0xFFE066, ALIGN_CENTER, 1, 255, 0x000000);

	/* the map list: a window of rows with a scroll bar */
	int listX = 70, listTop = 100, rowH = 31, listBottom = SH - 62;
	int rows = (listBottom - listTop) / rowH;
	int count = (int)maps.size();
	if (mapPicked < mapScroll) mapScroll = mapPicked;
	if (mapPicked >= mapScroll + rows) mapScroll = mapPicked - rows + 1;
	if (mapScroll > count - rows) mapScroll = count - rows;
	if (mapScroll < 0) mapScroll = 0;
	draw_text("Standard maps", listX, 68, 16, 0xFFFFFF, ALIGN_LEFT, 0, 190);
	fill_rect(listX, 92, 300, 1, 0xF8DDC2);
	fill_rect(listX, 93, 300, 1, 0x251002);
	for (int k = 0; k < rows && mapScroll + k < count; k++)
	{
		int i = mapScroll + k, y = listTop + k * rowH;
		bool on = i == mapPicked;
		draw_text(maps[i].name, listX + 8, y + 2, 18, on ? 0x89F9FF : 0xD0D0D0, ALIGN_LEFT, 0);
		draw_text(maps[i].size, listX + 280, y + 6, 11, on ? 0x89F9FF : 0x909090, ALIGN_RIGHT, 1);
		if (on)
			rect_outline(listX - 4, y - 4, 304, rowH - 2, 0x89F9FF, 150);
	}
	if (count > rows)
	{
		int trackY = listTop - 4, trackH = rows * rowH, barX = listX + 306;
		fill_rect(barX, trackY, 6, trackH, 0x303030);
		int thumbH = trackH * rows / count;
		if (thumbH < 16) thumbH = 16;
		int thumbY = trackY + (trackH - thumbH) * mapScroll / (count - rows);
		fill_rect(barX, thumbY, 6, thumbH, 0x89F9FF);
		if (mapScroll > 0) draw_text("^", barX + 3, trackY - 14, 11, 0x89F9FF, ALIGN_CENTER, 2);
		if (mapScroll + rows < count) draw_text("v", barX + 3, trackY + trackH + 1, 11, 0x89F9FF, ALIGN_CENTER, 2);
	}
	bool unlocked = have && endless_unlocked(maps[mapPicked]);
	if (!unlocked)
		playEndless = false;
	if (have && maps[mapPicked].logo.valid())
		draw_image(maps[mapPicked].logo, SW - 70 - maps[mapPicked].logo.w, 98);
	draw_text("MODE", 410, 166, 11, 0x909090, ALIGN_LEFT, 1);
	draw_text(playEndless ? "ENDLESS" : "CAMPAIGN", 410, 180, 20, playEndless ? 0xFFE066 : 0xFFFFFF, ALIGN_LEFT, 0);
	if (unlocked)
		draw_text("X: switch mode", 410, 210, 12, 0xB0B0B0, ALIGN_LEFT, 1);
	else if (have)
	{
		char b[140];
		snprintf(b, sizeof b, "Endless unlocks with a perfect\nscore on all three difficulties\nof this map  (%d / 3)", perfect_count(maps[mapPicked]));
		draw_text(b, 410, 210, 12, 0xB0B0B0, ALIGN_LEFT, 1);
	}
	draw_button("BACK", 40, SH - 54, 110, 26, false, 20);
	draw_button("PLAY", SW - 150, SH - 54, 110, 26, true, 20);
	draw_hint("Up/Down: map    L1/R1: difficulty    X: mode    A: play    B: back");
	if (pressed(JK_X) && unlocked) playEndless = !playEndless;
	if (pressed(JK_UP) && mapPicked > 0) mapPicked--;
	if (pressed(JK_DOWN) && mapPicked + 1 < count) mapPicked++;
	if (pressed(JK_L1) && difficulty > 1) difficulty--;
	if (pressed(JK_R1) && difficulty < 3)
	{
		if (difficulty < open) difficulty++;
		else lockedMsgUntil = millis() + 3000;
	}
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
static bool scoresAsk = false;      /* the "are you sure" box is open */
static int scoresAskSel = 0;        /* 0 = NO (the default), 1 = YES */

/* wipes everything the profile knows about one map: both score tables, the perfect marks (Endless closes again) and
 * the wins (Normal and Hard close again) */
static void clear_map_scores(const std::string &id)
{
	scoreTables.erase(id);
	scoreTables.erase("endless:" + id);
	perfectMask.erase(id);
	wonMask.erase(id);
	save_scores();
}

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
	if (scoresAsk)
	{
		fill_rect(0, 66, SW, SH - 66 - 24, 0x000000);   /* the table behind stays out of sight */
		int bx = 60, by = 70, bwid = SW - 120, bh = 200;
		fill_rect(bx - 2, by - 2, bwid + 4, bh + 4, 0xF8DDC2);
		fill_rect(bx, by, bwid, bh, 0x101010);
		draw_text("ARE YOU SURE?", SW / 2, by + 10, 24, 0xFF7070, ALIGN_CENTER, 0);
		char b[300];
		snprintf(b, sizeof b, "This erases ALL scores for %s,\nboth Campaign and Endless.", maps[scoresMap].name.c_str());
		draw_text(b, SW / 2, by + 48, 14, 0xFFFFFF, ALIGN_CENTER, 1);
		draw_text("Endless mode is locked again for this map,\nand Normal and Hard close until the map\nis won on Easy once more.", SW / 2, by + 88, 13, 0xFFE066, ALIGN_CENTER, 1);
		draw_text("This cannot be undone.", SW / 2, by + 140, 13, 0xFF9090, ALIGN_CENTER, 2);
		draw_button("NO", SW / 2 - 150, by + bh - 36, 110, 26, scoresAskSel == 0, 20);
		draw_button("YES, CLEAR", SW / 2 + 40, by + bh - 36, 130, 26, scoresAskSel == 1, 20);
		draw_hint("Left/Right: choose    A: confirm    B: cancel");
		if (pressed(JK_LEFT) || pressed(JK_RIGHT)) scoresAskSel ^= 1;
		if (pressed(JK_B)) scoresAsk = false;
		if (pressed(JK_A))
		{
			if (scoresAskSel == 1)
				clear_map_scores(key);
			scoresAsk = false;
		}
		return S_SCORES;
	}
	draw_hint("Left/Right: map    X: campaign / endless    Y: clear scores    B: back");
	if ((pressed(JK_LEFT) || pressed(JK_UP)) && scoresMap > 0) scoresMap--;
	if ((pressed(JK_RIGHT) || pressed(JK_DOWN)) && scoresMap + 1 < (int)maps.size()) scoresMap++;
	if (pressed(JK_X)) scoresEndless = !scoresEndless;
	if (pressed(JK_Y)) { scoresAsk = true; scoresAskSel = 0; }
	if (pressed(JK_B) || pressed(JK_START)) return S_MAIN;
	return S_SCORES;
}

static void draw_stats(const Result &r)
{
	const Stats &st = r.stats;
	char b[64];
	draw_text(r.endless ? "RUN OVER" : r.won ? "VICTORY!" : "DEFEATED", 24, 10, 30, r.endless || r.won ? 0xFFE066 : 0xFF7070, ALIGN_LEFT, 0);
	draw_text("MATCH STATS", SW - 24, 20, 18, 0xFFFFFF, ALIGN_RIGHT, 0);
	struct Col { int x; const char *head; } cols[3] = { { 24, "GOLD" }, { 232, "TOWERS BUILT" }, { 440, "ENEMIES" } };
	for (int i = 0; i < 3; i++)
	{
		draw_text(cols[i].head, cols[i].x, 56, 14, 0x89F9FF, ALIGN_LEFT, 0);
		fill_rect(cols[i].x, 76, 180, 1, 0xF8DDC2);
	}
	const int y0 = 82, dy = 16;   /* at most ten lines a column: they end above the buttons */
	int y = y0;
	auto row = [&](int x, const char *label, const std::string &val) {
		draw_text(label, x, y, 12, 0xE6E6E6, ALIGN_LEFT, 1);
		draw_text(val, x + 180, y, 12, 0xFFFFFF, ALIGN_RIGHT, 2);
		y += dy;
	};
	auto sub = [&](int x, const char *head) {
		draw_text(head, x, y + 2, 11, 0x89F9FF, ALIGN_LEFT, 0);
		y += dy;
	};
	auto num = [&](int v) { snprintf(b, sizeof b, "%d", v); return std::string(b); };
	/* gold, then what became of the towers */
	row(24, "Earned from enemies", num(st.goldEarned));
	row(24, "Depot bonus (in that)", num((int)st.depotGold));
	row(24, "Spent on towers, traps", num(st.goldSpent));
	row(24, "Gained selling", num(st.goldSold));
	row(24, "Left over", num(r.money));
	sub(24, "TOWER UPKEEP");
	row(24, "Upgrades", num(st.upgrades));
	row(24, "Repairs", num(st.repairs));
	row(24, "Sold", num(st.sold));
	row(24, "Lost in battle", num(st.towersLost));
	/* the towers */
	y = y0;
	int total = 0;
	for (size_t i = 0; i < r.towerNames.size() && i < 8; i++)
	{
		row(232, r.towerNames[i].c_str(), num(st.built[i]));
		total += st.built[i];
	}
	fill_rect(232, y, 180, 1, 0x707070);
	y += 4;
	row(232, "In all", num(total));
	sub(232, "BASTION");
	row(232, "Research completed", num(st.researchDone));
	row(232, "Actions used", num(st.specialsUsed));
	row(232, "Soldiers sent", num(st.soldiersSent));
	row(232, "Soldier kills", num(st.soldierKills));
	/* the enemies, and the traps */
	y = y0;
	row(440, "Defeated", num(st.enemiesDefeated));
	row(440, "Reached the end", num(st.enemiesLeaked));
	row(440, "Shots fired", num(st.shots));
	int sec = st.playMs / 1000;
	snprintf(b, sizeof b, "%d:%02d", sec / 60, sec % 60);
	row(440, "Time played", b);
	row(440, "Lives left", num(r.lives));
	if (!r.trapNames.empty())
	{
		sub(440, "TRAPS PLACED");
		for (size_t i = 0; i < r.trapNames.size() && i < 4; i++)
			row(440, r.trapNames[i].c_str(), num(st.trapsBuilt[i]));
		if (st.boulderThrows) row(440, "Boulders thrown at towers", num(st.boulderThrows));
	}
}

static int afterView = getenv("DM_AFTERVIEW") ? 1 : 0;   /* test hook: open on the match stats */

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

	if (r.newUnlock)
		draw_text(r.newUnlock == 2 ? "NORMAL DIFFICULTY UNLOCKED!" : "HARD DIFFICULTY UNLOCKED!", 24, y + (r.score >= r.perfect && r.perfect > 0 ? 62 : 80), 14, 0x89F9FF, ALIGN_LEFT, 2, 255, 0x000000);

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
	int menuKind = 0;       /* 0 none, 1 build, 2 tower, 3 trap, 4 the Bastion */
	int bTab = 0, bCol = 0, bSel = 0;   /* the Bastion panel: tab (research, actions), column and row */
	float trapX = 0, trapY = 0;   /* where the trap menu is open (on the path) */
	int menuSel = 0, menuTX = 0, menuTY = 0;
	Tower *menuTower = NULL;
	int started = 0;
	int quitSel = 1;
	int speed2 = 0;
	uint32_t toastUntil = 0;   /* a short notice in the middle of the top edge */
	const char *toast = "";
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
	camX &= ~1;   /* an even column keeps the map copy word-aligned, which is much faster on this CPU */
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
	int count = p.menuKind == 1 ? (int)m->towerTypes.size() : p.menuKind == 3 ? (int)m->trapTypes.size() : 3;
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
		else if (p.menuKind == 3)
		{
			img = &m->trapTypes[i].icon;
			cost = m->trapTypes[i].costs;
			dim = m->money < cost || m->trapCount(i) >= m->trapCap(i);
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
	if (p.menuKind == 3)
	{
		const TrapType &t = m->trapTypes[p.menuSel];
		char b[300];
		snprintf(b, sizeof b, "%s - %d $   (%d of %d placed)\n%s", t.name.c_str(), t.costs, m->trapCount(p.menuSel), m->trapCap(p.menuSel), t.text.c_str());
		info_box(b);
	}
	else if (p.menuKind == 1)
	{
		const TowerType &t = m->towerTypes[p.menuSel];
		char b[300];
		if (t.support)
			snprintf(b, sizeof b, "%s - %d $\n%s", t.name.c_str(), t.costs, support_text(t.sup[0], m->tileSize).c_str());
		else
			snprintf(b, sizeof b, "%s - %d $\nDMG: %d   FR: %.1fs   %s%s%s", t.name.c_str(), t.costs, t.damage, (float)t.fireRate / 1000, t.description.c_str(), t.groundOnly ? "  (ground only)" : "", t.chill[0] > 0 ? "  (slowed take more damage)" : "");
		info_box(b);
	}
	else if (p.menuTower)
		info_box(p.menuTower->name + "\n" + p.menuTower->description());
}

static void close_menu(Play &p)
{
	if (p.menuKind == 4 && p.map && !p.map->pause) globalSpeed = p.speed2 ? 2.0f : 1.0f;   /* the Bastion panel stops the game while it is open */
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

/* ------------------------------------------------------------------ the Bastion panel */
static void bastion_rows(const Map *m, int col, std::vector<int> &rows)
{
	rows.clear();
	for (size_t i = 0; i < m->research.size(); i++)
		if ((m->research[i].group != "tower" && m->research[i].group != "trap") == (col == 1)) rows.push_back((int)i);
}

static void bastion_open(Play &p)
{
	p.menuKind = 4;
	p.bTab = 0; p.bCol = 0; p.bSel = 0;
	globalSpeed = 0;
}

static void bastion_input(Play &p)
{
	Map *m = p.map;
	if (pressed(JK_B)) { close_menu(p); return; }
	if (pressed(JK_L1) || pressed(JK_R1) || pressed(JK_X)) { p.bTab ^= 1; p.bSel = 0; p.bCol = 0; }
	if (p.bTab == 0)
	{
		std::vector<int> rows;
		if (pressed(JK_LEFT) || pressed(JK_RIGHT)) { p.bCol ^= 1; p.bSel = 0; }
		bastion_rows(m, p.bCol, rows);
		if (rows.empty()) return;
		if (pressed(JK_UP)) p.bSel = (p.bSel + (int)rows.size() - 1) % (int)rows.size();
		if (pressed(JK_DOWN)) p.bSel = (p.bSel + 1) % (int)rows.size();
		if (p.bSel >= (int)rows.size()) p.bSel = 0;
		if (pressed(JK_A))
		{
			if (!m->startResearch(rows[p.bSel])) { sfx_play(SFX_ERROR); }
		}
	}
	else
	{
		if (pressed(JK_UP)) p.bSel = (p.bSel + 7) % 8;
		if (pressed(JK_DOWN)) p.bSel = (p.bSel + 1) % 8;
		if (pressed(JK_A))
		{
			if (!m->useSpecial(p.bSel)) sfx_play(SFX_ERROR);
			else close_menu(p);
		}
	}
}

static void bastion_draw(const Play &p)
{
	Map *m = p.map;
	fill_rect(10, 6, SW - 20, SH - 12, 0x0C1420, 255);
	rect_outline(10, 6, SW - 20, SH - 12, 0x89F9FF, 150);
	draw_text("BASTION", 26, 10, 22, 0xFFE066, ALIGN_LEFT, 0);
	{ char g[40]; snprintf(g, sizeof g, "Gold %d", m->money); draw_text(g, SW - 26, 14, 16, 0xFFD400, ALIGN_RIGHT, 1); }
	draw_text("RESEARCH", 250, 17, 14, p.bTab == 0 ? 0xFFFFFF : 0x808080, ALIGN_LEFT, 0);
	draw_text("ACTIONS", 350, 17, 14, p.bTab == 1 ? 0xFFFFFF : 0x808080, ALIGN_LEFT, 0);
	fill_rect(p.bTab == 0 ? 248 : 348, 35, p.bTab == 0 ? 80 : 66, 2, 0x89F9FF, 255);
	char b[200];
	std::string why, l1, l2;
	if (p.bTab == 0)
	{
		const char *heads[2] = { "TOWERS AND TRAPS", "KEEP AND GARRISON" };
		for (int c = 0; c < 2; c++)
		{
			int x = 26 + c * 300;
			draw_text(heads[c], x, 44, 11, 0x89F9FF, ALIGN_LEFT, 0);
			std::vector<int> rows;
			bastion_rows(m, c, rows);
			for (size_t i = 0; i < rows.size(); i++)
			{
				const ResearchLine &r = m->research[rows[i]];
				int y = 60 + (int)i * 16;
				bool sel = c == p.bCol && (int)i == p.bSel;
				if (sel) fill_rect(x - 4, y - 2, 280, 16, 0x89F9FF, 60);
				bool active = m->researchActive == rows[i];
				draw_text(r.title, x, y, 12, sel ? 0xFFFFFF : 0xD0D0D0, ALIGN_LEFT, 1);
				for (int k = 0; k < 3; k++)
				{
					uint32_t col = k < r.done ? 0xFFD400 : (active && k == r.done ? 0x89F9FF : 0x505860);
					fill_circle(x + 190 + k * 18, y + 7, 5, col, 255);
				}
			}
		}
		std::vector<int> rows;
		bastion_rows(m, p.bCol, rows);
		if (!rows.empty())
		{
			int line = rows[p.bSel < (int)rows.size() ? p.bSel : 0];
			const ResearchLine &r = m->research[line];
			fill_rect(26, 212, SW - 52, 1, 0xF8DDC2, 200);
			if (r.done >= (int)r.tier.size())
			{
				snprintf(b, sizeof b, "%s: fully researched", r.title.c_str());
				draw_text(b, 26, 220, 13, 0xFFD400, ALIGN_LEFT, 1);
			}
			else
			{
				const ResearchTier &t = r.tier[r.done];
				snprintf(b, sizeof b, "%s  -  tier %d of %d:  %s", r.title.c_str(), r.done + 1, (int)r.tier.size(), t.name.c_str());
				draw_text(b, 26, 220, 13, 0xFFFFFF, ALIGN_LEFT, 1);
				draw_text(t.text, 26, 236, 12, 0xE6E6E6, ALIGN_LEFT, 1);
				bool waveOn = m->waves && m->waves->started && m->waves->current < (int)m->waves->waves.size() && m->waves->waves[m->waves->current]->begun();
				snprintf(b, sizeof b, "Costs %d gold, takes %d %s%s", t.cost, t.waves, t.waves == 1 ? "wave" : "waves", waveOn ? " (the wave under way does not count)" : " once started");
				draw_text(b, 26, 252, 12, m->money >= t.cost ? 0xB0FFB0 : 0xFF9090, ALIGN_LEFT, 1);
				why = m->researchProblem(line);
				if (!why.empty()) draw_text(why, 26, 268, 12, 0xFF9090, ALIGN_LEFT, 1);
				else draw_text("A: start", 26, 268, 12, 0x89F9FF, ALIGN_LEFT, 1);
			}
			{   /* the doctrine of the line's path: tiers finished anywhere in it add up */
				const std::string &g = r.group;
				int cat = g == "tower" ? 0 : g == "trap" ? 1 : g == "garrison" ? 2 : r.id == "fortify" ? -1 : 3;
				static const char *nm[4] = { "Towers", "Traps", "Garrison", "Command" };
				if (cat >= 0)
				{
					int n = m->pathTiers(cat), lv = m->doctrineLevel(cat);
					if (lv >= 3) snprintf(b, sizeof b, "%s doctrine: %d tiers done, level 3 (the highest)", nm[cat], n);
					else snprintf(b, sizeof b, "%s doctrine: %d tiers done, level %d - the next level at %d", nm[cat], n, lv, (lv + 1) * 3);
					draw_text(b, 26, 284, 12, 0xFFD400, ALIGN_LEFT, 1);
				}
			}
		}
		if (m->researchActive >= 0)
		{
			const ResearchLine &r = m->research[m->researchActive];
			snprintf(b, sizeof b, "Under way: %s - %s   (%d %s to go)", r.title.c_str(), r.tier[r.done].name.c_str(), m->researchLeft, m->researchLeft == 1 ? "wave" : "waves");
			draw_text(b, 26, 304, 12, 0x89F9FF, ALIGN_LEFT, 1);
		}
		else
			draw_text("No research under way: one project at a time", 26, 304, 12, 0x909090, ALIGN_LEFT, 1);
		draw_text("The game is paused here.  D-Pad: choose   L1/R1: tab   A: start (it cannot be stopped)   B: close", SW / 2, SH - 34, 10, 0xB0B0B0, ALIGN_CENTER, 1);
	}
	else
	{
		static const char *names[8] = { "Volley", "Rally", "Mend", "Frost Horn", "Vermin Hunters", "Giant Slayers", "Skyguard", "Boulder Rollers" };
		static const char *ids[8] = { "volley", "rally", "mend", "frost", "vermin", "giant", "sky", "roller" };
		for (int i = 0; i < 8; i++)
		{
			int y = 52 + i * 20 + (i >= 4 ? 14 : 0);
			bool sel = i == p.bSel;
			if (sel) fill_rect(22, y - 3, SW - 44, 20, 0x89F9FF, 60);
			draw_text(names[i], 34, y, 13, sel ? 0xFFFFFF : 0xD0D0D0, ALIGN_LEFT, 1);
			int lvl = m->specialLevel(ids[i]);
			std::string st = lvl <= 0 ? "not researched" : m->specialProblem(i).empty() ? "READY" : m->specialProblem(i);
			if (i == 1 && (int)gnow() < m->rallyUntil) st = "ACTIVE";
			draw_text(st, 300, y, 12, st == "READY" ? 0xB0FFB0 : st == "ACTIVE" ? 0xFFD400 : 0x909090, ALIGN_LEFT, 1);
			for (int k = 0; k < 3; k++) fill_circle(540 + k * 18, y + 6, 5, k < lvl ? 0xFFD400 : 0x505860, 255);
		}
		draw_text("THE GARRISON", 34, 52 + 4 * 20 + 1, 10, 0x89F9FF, ALIGN_LEFT, 0);
		int line = m->specialLine(ids[p.bSel]);
		fill_rect(26, 228, SW - 52, 1, 0xF8DDC2, 200);
		if (line >= 0)
		{
			const ResearchLine &r = m->research[line];
			int lvl = r.done;
			const ResearchTier &t = r.tier[lvl > 0 ? lvl - 1 : 0];
			static const char *what[4] = { "Arrows rain down on every creature near the keep, each losing a share of its health.",
				"Every tower fires faster for a short while.", "Every damaged tower is repaired by a share of its health.",
				"A cold blast slows every creature on the path for a short while." };
			std::string desc = p.bSel < 4 ? what[p.bSel] : (p.bSel - 4 < (int)m->soldierTypes.size() ? m->soldierTypes[p.bSel - 4].text : "");
			draw_text(desc, 26, 236, 12, 0xFFFFFF, ALIGN_LEFT, 1);
			snprintf(b, sizeof b, "%s%s", lvl > 0 ? "Now: " : "Research it first. ", t.text.c_str());
			draw_text(b, 26, 254, 12, 0xE6E6E6, ALIGN_LEFT, 1);
			if (lvl > 0 && lvl < (int)r.tier.size())
			{
				snprintf(b, sizeof b, "Next tier: %s", r.tier[lvl].text.c_str());
				draw_text(b, 26, 272, 12, 0x89F9FF, ALIGN_LEFT, 1);
			}
		}
		draw_text("The game is paused here.  Up/Down: choose   A: use it   L1/R1: tab   B: close", SW / 2, SH - 34, 10, 0xB0B0B0, ALIGN_CENTER, 1);
	}
}

/* A tower that enemies destroy is removed and freed by the map while its menu may still be open: the menu lets go of
 * it without touching it (it may already be gone) as soon as it is no longer one of the map's towers. */
static void drop_menu_if_tower_gone(Play &p)
{
	if (!p.menuTower)
		return;
	for (size_t i = 0; i < p.map->towers.size(); i++)
		if (p.map->towers[i] == p.menuTower)
			return;
	p.menuTower = NULL;
	p.menuKind = 0;
}

/* moves the cursor with the D-pad, faster the longer it is held; near the screen edge the map scrolls */
static void move_cursor(Play &p, float dx, float dy, float dtMs)
{
	Map *m = p.map;
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
	if (p.menuKind == 4) { bastion_input(p); return; }   /* before Start, Select and R1: the panel uses L1/R1 for its tabs and keeps the game stopped */
	if (pressed(JK_Y))
		slotcheck_mark();   /* only does anything with slot_check = 1: saves the driver's frame ring right now */
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
	if ((pressed(JK_R1) || pressed(JK_R2)) && !m->pause && !m->defeated)
	{
		p.speed2 = !p.speed2;
		globalSpeed = p.speed2 ? 2.0f : 1.0f;
		p.toast = p.speed2 ? "FAST FORWARD  x2" : "NORMAL SPEED";
		p.toastUntil = millis() + 1500;
	}
	if (pressed(JK_X))
		m->showBuildAreas = !m->showBuildAreas;
	if (m->pause || m->defeated)
		return;
	if (p.menuKind)
	{
		int count = p.menuKind == 1 ? (int)m->towerTypes.size() : p.menuKind == 3 ? (int)m->trapTypes.size() : 3;
		if (pressed(JK_LEFT) || pressed(JK_UP)) p.menuSel = (p.menuSel + count - 1) % count;
		if (pressed(JK_RIGHT) || pressed(JK_DOWN)) p.menuSel = (p.menuSel + 1) % count;
		if (pressed(JK_B)) close_menu(p);
		else if (pressed(JK_A))
		{
			if (p.menuKind == 3)
			{
				if (m->addTrap(p.menuSel, p.trapX, p.trapY)) p.menuKind = 0;
				else
				{
					sfx_play(SFX_ERROR);
					p.toast = "CANNOT PLACE THAT HERE";
					std::string why = m->trapProblem(p.menuSel, p.trapX, p.trapY);
					static std::string last;
					last = why;
					p.toast = last.c_str();
					p.toastUntil = millis() + 1800;
				}
			}
			else if (p.menuKind == 1)
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
		move_cursor(p, dx, dy, dtMs);
	else
		p.holdMs = 0;
	if (pressed(JK_A) && !m->defeated)
	{
		int wx = (int)p.cx + camX, wy = (int)p.cy + camY;
		int j = m->towerAt(wx, wy);
		if (m->nearBastion((float)wx, (float)wy))
			bastion_open(p);
		else if (j != -1)
			open_tower_menu(p, j);
		else if (m->canBuild(wx, wy))
		{
			p.menuKind = 1;
			p.menuSel = 0;
			p.menuTX = wx / m->tileSize;
			p.menuTY = wy / m->tileSize;
		}
		else
		{
			float qx, qy;
			if (!m->trapTypes.empty() && m->nearPath((float)wx, (float)wy, 30, qx, qy))   /* on the path: the trap menu */
			{
				p.menuKind = 3;
				p.menuSel = 0;
				p.trapX = qx;
				p.trapY = qy;
				p.menuTX = (int)qx / m->tileSize;
				p.menuTY = (int)qy / m->tileSize;
			}
		}
	}
}

static void play_draw(Play &p)
{
	Map *m = p.map;
	m->display();   /* may free a destroyed tower */
	drop_menu_if_tower_gone(p);
	m->displayActions();
	/* the tile under the cursor */
	if (!p.menuKind && !m->quitDialog && !m->pause)
	{
		int wx = (int)p.cx + camX, wy = (int)p.cy + camY;
		int tx = wx / m->tileSize, ty = wy / m->tileSize;
		bool buildable = m->canBuild(wx, wy) && m->towerAt(wx, wy) == -1;
		bool onTower = m->towerAt(wx, wy) != -1;
		float qx, qy;
		bool trapSpot = !onTower && !buildable && !m->trapTypes.empty() && m->nearPath((float)wx, (float)wy, 30, qx, qy);
		bool onKeep = m->nearBastion((float)wx, (float)wy);
		uint32_t col = onKeep ? 0x89F9FF : onTower ? 0xFFE066 : buildable ? 0x66FF66 : trapSpot ? 0xFFAA33 : 0xFF5050;
		rect_outline(tx * m->tileSize - camX, ty * m->tileSize - camY, m->tileSize, m->tileSize, col, 190);
		if (onKeep) draw_text("A: BASTION", p.cx, p.cy + 14, 12, 0x89F9FF, ALIGN_CENTER, 2, 255, 0x000000);
		/* the pointer */
		int x = (int)p.cx, y = (int)p.cy;
		draw_line(x - 8, y, x + 8, y, 0x000000);
		draw_line(x, y - 8, x, y + 8, 0x000000);
		draw_line(x - 7, y, x + 7, y, 0xFFFFFF);
		draw_line(x, y - 7, x, y + 7, 0xFFFFFF);
	}
	if (p.menuKind == 3 && p.menuSel < (int)m->trapTypes.size())
	{   /* where the trap will go, and what it reaches */
		const TrapType &tt = m->trapTypes[p.menuSel];
		float gx = p.trapX - camX, gy = p.trapY - camY;
		bool bad = !m->trapProblem(p.menuSel, p.trapX, p.trapY).empty();
		fill_circle(gx, gy, tt.radius, bad ? 0xFF6060 : 0xFFD070, 50);
		circle_outline(gx, gy, tt.radius, bad ? 0xFF6060 : 0xFFD070, 220);
		draw_image_center(tt.picture, gx, gy, 0, 0, -1, -1, 170, 0xFFFFFF);
	}
	if (p.menuKind && p.menuKind != 4)
		draw_radial(p);
	m->drawHud();
	if (p.menuKind == 4)
		bastion_draw(p);   /* over the HUD: it has its own gold line */
	if (m->pause && !m->quitDialog)
		draw_text("PAUSED", SW / 2, SH / 2 - 14, 28, 0xFFFFFF, ALIGN_CENTER, 2);
	if (p.speed2 && !m->pause)
		draw_text("x2", SW - 70, 19, 14, 0xFFE066, ALIGN_RIGHT, 2, 255, 0x000000);
	if ((int)millis() < m->noticeUntil)
		draw_text(m->notice, SW / 2, 68, 18, 0xFFD400, ALIGN_CENTER, 2, 255, 0x000000);
	if (millis() < p.toastUntil)
		draw_text(p.toast, SW / 2, 44, 16, 0xFFE066, ALIGN_CENTER, 2, 255, 0x000000);
	if (m->quitDialog)
	{
		fill_rect(SW / 2 - 150, SH / 2 - 56, 300, 112, 0x000000, 190);
		rect_outline(SW / 2 - 150, SH / 2 - 56, 300, 112, 0x89F9FF, 160);
		draw_text("Do you really want to quit\nand go back to the menu?", SW / 2, SH / 2 - 44, 12, 0xFFFFFF, ALIGN_CENTER, 1);
		draw_button("NO", SW / 2 - 110, SH / 2 + 12, 70, 26, p.quitSel == 0, 18);
		draw_button("YES", SW / 2 + 40, SH / 2 + 12, 70, 26, p.quitSel == 1, 18);
	}
	else if (!p.menuKind && !m->gameStarted && (int)millis() - p.started < 15000 && !m->pause)
		draw_text("D-Pad: move   A: build / select   B: cancel   X: show build spots   R1 / R2: x2 speed   Start: pause   Select: menu",
		          SW / 2, SH - 84, 10, 0xFFFFFF, ALIGN_CENTER, 2, 255, 0x000000);
}

/* ------------------------------------------------------------------ on-device test run (DefensemaniaDbg) */
/* With DM_AUTOMAP set the game skips the menus, plays that map by itself (DM_AUTOPLAY builds and upgrades towers, at
 * DM_AUTOSPEED times normal speed), prints a status line every 300 frames and a final "DBG RESULT" line, then exits.
 * The run script of the debug launcher uses it for stress tests whose logs end up on the card. */
static void proc_mem_kb(long &rss, long &peak)
{
	rss = peak = -1;
	FILE *f = fopen("/proc/self/status", "r");
	if (!f)
		return;
	char line[120];
	while (fgets(line, sizeof line, f))
	{
		long v;
		if (sscanf(line, "VmRSS: %ld", &v) == 1) rss = v;
		else if (sscanf(line, "VmHWM: %ld", &v) == 1) peak = v;
	}
	fclose(f);
}

/* ------------------------------------------------------------------ crash report */
/* A fatal signal writes where it happened to log.txt (stderr): the fault address, the program counter and return
 * address, the screen and map, and every word of the stack that looks like a code address, so the unstripped
 * binary can name the function afterwards. */
static volatile int g_screenNow = 0;
static char g_mapNow[64] = "";
extern "C" char __executable_start, etext;
#ifdef __mips__
static void crash_report(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc = (ucontext_t *)ctx;
	unsigned long pc = (unsigned long)uc->uc_mcontext.pc, ra = (unsigned long)uc->uc_mcontext.gregs[31];
	unsigned long sp = (unsigned long)uc->uc_mcontext.gregs[29];
	fprintf(stderr, "CRASH signal %d at address %p, pc %lx, ra %lx, sp %lx, base %lx, screen %d, map '%s'\n", sig, si->si_addr, pc, ra, sp, (unsigned long)&__executable_start, (int)g_screenNow, g_mapNow);
	unsigned long cv[3];
	gfx_canvas_info(cv);
	fprintf(stderr, "CRASH canvas %lx %lux%lu\n", cv[0], cv[1], cv[2]);
	fprintf(stderr, "CRASH stack:");
	int shown = 0;
	for (unsigned long a = sp; a < sp + 4096 && shown < 40; a += 4)
	{
		unsigned long v = *(unsigned long *)a;
		if (v >= (unsigned long)&__executable_start && v < (unsigned long)&etext)
		{
			fprintf(stderr, " %lx", v);
			shown++;
		}
	}
	fprintf(stderr, "\n");
	fflush(stderr);
	_exit(139);
}
#endif
static void crash_report_install()
{
#ifdef __mips__   /* the report reads MIPS registers; other builds (the address-sanitizer test build) keep their own handlers */
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = crash_report;
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
#endif
}

/* ------------------------------------------------------------------ main */
int disptest_run();   /* disptest.cpp */

int main()
{
	crash_report_install();
	if (!gfx_init())
		return 1;
	slotcheck_start();   /* does nothing unless slot_check = 1 in defensemania.cfg */
	if (getenv("DM_DISPTEST"))   /* the display test of DefensemaniaDbg: no game, only frames sent to the driver and read back */
	{
		int rc = disptest_run();
		hwdisp_deinit();
		SDL_Quit();
		return rc;
	}
	load_image("data/logo_dm.png", logo);
	load_image("data/repair_icon.png", repairIcon);
	load_image("data/update_icon.png", updateIcon);
	load_image("data/sell_icon.png", sellIcon);
	load_maps();
	optShowFps = cfg_num("show_fps", 0) != 0;
	optHQ = cfg_num("hq_graphics", 0) != 0;
	audio_init();
	load_settings();
	if (getenv("DM_HQ")) optHQ = atoi(getenv("DM_HQ")) != 0;   /* test hook */
	load_scores();
	music_play_folder("Conquest");

	Screen screen = S_MAIN;
	const bool autoRun = getenv("DM_AUTOMAP") != NULL;
	int autoFrames = 0, autoLimit = getenv("DM_AUTOFRAMES") ? atoi(getenv("DM_AUTOFRAMES")) : 6000, autoMinFps = 999, autoStart = 0;
	if (autoRun)
	{
		mapPicked = atoi(getenv("DM_AUTOMAP"));
		if (mapPicked < 0 || mapPicked >= (int)maps.size()) mapPicked = 0;
		difficulty = getenv("DM_AUTODIFF") ? atoi(getenv("DM_AUTODIFF")) : 1;
		playEndless = getenv("DM_AUTOENDLESS") != NULL;
		screen = S_LOADING;
		printf("DBG START map %d (%s) difficulty %d seed %s frames %d speed %s\n", mapPicked, maps.empty() ? "?" : maps[mapPicked].name.c_str(), difficulty,
		       getenv("DM_SEED") ? getenv("DM_SEED") : "default", autoLimit, getenv("DM_AUTOSPEED") ? getenv("DM_AUTOSPEED") : "4");
		fflush(stdout);
	}
	int mainSel = 0, optSel = 0, afterSel = 0, helpPage = 0;
	if (getenv("DM_HELP")) { screen = S_HELP; helpPage = atoi(getenv("DM_HELP")); }   /* test hook: open on a page of How To Play */
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
		static int hitches;
		static int partsPlay, partsMusic, partsPresent;   /* where the last frame spent its time */
		if (dt > (getenv("DM_HITCH_MS") ? atof(getenv("DM_HITCH_MS")) : 80) && screen == S_GAME && hitches < 30)   /* a pause in the frames, noted in log.txt */
		{
			hitches++;
			printf("frame hitch: %.0f ms (the frame before spent %d ms in the game, %d ms in the music, %d ms presenting; drawing: map %d, list %d, sprites %d, bars/shots %d ms; %d enemies, %d towers%s)\n", dt, partsPlay, partsMusic, partsPresent,
			       g_prof[0], g_prof[1], g_prof[2], g_prof[3], g_profEnemies, g_profTowers, play.map && play.map->showBuildAreas ? ", build areas shown" : "");
			fflush(stdout);
		}
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
		g_screenNow = screen;
		uint32_t tA = millis();
		if (play.map) snprintf(g_mapNow, sizeof g_mapNow, "%s", maps[lastMap].name.c_str());
		switch (screen)
		{
		case S_MAIN: next = screen_main(mainSel); break;
		case S_PLAY: next = screen_play(); break;
		case S_HELP: next = screen_help(helpPage); break;
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
			if (getenv("DM_BUILDAREAS")) play.map->showBuildAreas = true;   /* test hook */
			if (getenv("DM_BASTION") && play.map->ok)   /* test hook: opens the Bastion panel (tab 0 research or 1 actions) with a few tiers researched */
			{
				Map *bm = play.map;
				for (size_t k = 0; k < bm->research.size() && k < 18; k++) bm->research[k].done = (int)(k % 4 > 2 ? 2 : k % 4);
				bm->researchActive = 0; bm->researchLeft = 2; bm->money = 300;
				bastion_open(play);
				play.bTab = atoi(getenv("DM_BASTION")) & 1;
				if (getenv("DM_BASTION_SEL")) play.bSel = atoi(getenv("DM_BASTION_SEL"));
			}
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
				lastFrame = millis();   /* the time spent loading is not a hitch */
				padPrev = padNow = gfx_pad();
				music_play_folder(music_folder_for(maps[mapPicked].url, maps[mapPicked].name));
			}
			break;
		case S_GAME:
			if (getenv("DM_CAMKEEP") && play.map->ok) { camX = (int)play.map->keepX - SW / 2 + atoi(getenv("DM_CAMKEEP")); camY = (int)play.map->keepY - SH / 2 - 20; camera_limits(play.map); }   /* test hook: look at the keep */
			if (getenv("DM_CRASH_TEST") && (int)millis() - play.started > 4000) *(volatile int *)0 = 1;
			if (getenv("DM_SOLDIERS") && play.map->ok && play.map->waves && play.map->waves->started && play.map->waves->waves[play.map->waves->current]->begun())
			{   /* test hook: once the first wave is on, sends one soldier of each kind, to see them at work */
				static bool sent;
				if (!sent)
				{
					sent = true;
					ResearchTier t;
					t.eff.push_back(std::make_pair(std::string("count"), 2.0f)); t.eff.push_back(std::make_pair(std::string("time"), 90.0f)); t.eff.push_back(std::make_pair(std::string("zone"), 700.0f));
					for (int k = 0; k < 4; k++) play.map->spawnSoldiers(k, t);
				}
				static int since;
				if (sent && getenv("DM_SOLDKILL") && ++since == atoi(getenv("DM_SOLDKILL")))   /* and, if asked, kills them all some frames later, to see them fall */
				{ gfx_mark_frame(); for (size_t k = 0; k < play.map->soldiers.size(); k++) play.map->soldiers[k].health = -1; }
			}
			if (getenv("DM_FAKE_RESULT") && (int)millis() - play.started > (getenv("DM_FAKE_DELAY") ? atoi(getenv("DM_FAKE_DELAY")) : 1500) && !play.map->finished)
			{   /* test hook: end the match at once with some score */
				play.map->killPoints = 640;
				play.map->wavesCleared = atoi(getenv("DM_FAKE_RESULT"));
				play.map->finished = true;
				play.map->finishedWon = getenv("DM_FAKE_WIN") != NULL;
			}
			if (getenv("DM_AUTOPLAY") || getenv("DM_AUTOMAP"))   /* test hook: builds, upgrades, repairs and sells towers by itself, at speed */
			{
				static unsigned rnd = getenv("DM_SEED") ? (unsigned)atoi(getenv("DM_SEED")) : 12345;
				static int n;
				auto next = [&]() { rnd = rnd * 1103515245u + 12345u; return (int)((rnd >> 8) & 0xFFFF); };
				globalSpeed = getenv("DM_AUTOSPEED") ? (float)atof(getenv("DM_AUTOSPEED")) : 4.0f;
				Map *m = play.map;
				if (++n % 25 == 0 && !m->towerTypes.empty())
				{
					int tx = next() % m->cols, ty = next() % m->rows;
					if (m->canBuild(tx * m->tileSize + 4, ty * m->tileSize + 4) && m->towerAt(tx * m->tileSize + 4, ty * m->tileSize + 4) == -1)
						m->addTower(next() % (int)m->towerTypes.size(), tx, ty);
				}
				if (n % 40 == 0 && !m->towers.empty())
				{
					Tower *t = m->towers[next() % m->towers.size()];
					if (!t->destroyed) t->upgrade();
				}
				if (n % 90 == 0 && !m->towers.empty())
					m->towers[next() % m->towers.size()]->repair();
				if (n % 300 == 0 && m->towers.size() > 6)
					m->sellTower(m->towers[next() % m->towers.size()]);
				if (!getenv("DM_AUTOPAN"))   /* wander over the map like a player does, and open and close tower menus */
				{
					static int dirFrames;
					static float adx, ady;
					if (--dirFrames <= 0)
					{
						int r = next() % 9;
						adx = (float)(r % 3 - 1);
						ady = (float)(r / 3 - 1);
						dirFrames = 20 + next() % 120;
					}
					if (play.menuKind == 0 && (adx || ady))
						move_cursor(play, adx, ady, dt);
					if (n % 120 == 0 && play.menuKind == 0 && !m->towers.empty())
						open_tower_menu(play, next() % (int)m->towers.size());
					else if (n % 120 == 45 && play.menuKind)
						close_menu(play);
				}
			}
			drop_menu_if_tower_gone(play);
			play_input(play, dt);
			drop_menu_if_tower_gone(play);
			play_draw(play);
			if (autoRun)
			{
				if (!autoStart) autoStart = (int)millis();
				autoFrames++;
				if (fps > 0 && fps < autoMinFps && (int)millis() - autoStart > 8000) autoMinFps = fps;
				Map *am = play.map;
				static float abortMs;   /* Select+Start ends this run; held for 3 seconds it ends the whole plan too */
				if (held(JK_START) && held(JK_SELECT)) abortMs += dt; else abortMs = 0;
				if (abortMs > 0 && (abortMs > 3000 || autoFrames > 20) && (abortMs > 600))
				{
					if (abortMs > 3000)
					{
						FILE *sf = fopen("dbg/STOP", "w");
						if (sf) { fputs("stop\n", sf); fclose(sf); }
					}
					printf("DBG RESULT frames %d  stopped with the pad%s\n", autoFrames, abortMs > 3000 ? " (plan stopped)" : "");
					fflush(stdout);
					screen = next = S_QUIT;
					break;
				}
				static long sumMap, sumList, sumSprites, sumBars;
				sumMap += g_prof[0]; sumList += g_prof[1]; sumSprites += g_prof[2]; sumBars += g_prof[3];
				if (autoFrames % 300 == 0)
				{
					long rss, peak;
					proc_mem_kb(rss, peak);
					printf("DBG frame %d  wave %d/%d  towers %d  money %d  lives %d  fps %d  rss %ld kB (peak %ld)  avg ms per frame: map+areas %.1f, list %.1f, sprites %.1f, bars/shots %.1f%s%s\n", autoFrames, am->waves->current + 1, (int)am->waves->waves.size(),
					       (int)am->towers.size(), am->money, am->lifes, fps, rss, peak, sumMap / 300.0, sumList / 300.0, sumSprites / 300.0, sumBars / 300.0, optHQ ? "  [HQ graphics]" : "", am->showBuildAreas ? "  [build areas]" : "");
					{
						int dep = 0, sup = 0;
						float best = 1;
						for (size_t i = 0; i < am->towers.size(); i++)
						{
							if (am->towers[i]->support) dep++;
							else if (am->towers[i]->buffRate > 1.0f) { sup++; if (am->towers[i]->buffRate > best) best = am->towers[i]->buffRate; }
						}
						if (dep) printf("DBG depots %d, towers supplied %d (best fire rate x%.2f)\n", dep, sup, best);
					}
					fflush(stdout);
					sumMap = sumList = sumSprites = sumBars = 0;
				}
				if (autoFrames >= autoLimit && !am->finished)
				{
					long rss, peak;
					proc_mem_kb(rss, peak);
					printf("DBG RESULT frames %d  ended by the frame limit  wave %d/%d  lives %d  min fps %d  rss %ld kB (peak %ld)\n", autoFrames, am->waves->current + 1, (int)am->waves->waves.size(), am->lifes, autoMinFps == 999 ? -1 : autoMinFps, rss, peak);
					fflush(stdout);
					screen = next = S_QUIT;
					break;
				}
				if (am->finished)
				{
					long rss, peak;
					proc_mem_kb(rss, peak);
					printf("DBG RESULT frames %d  match %s  wave %d/%d  lives %d  min fps %d  rss %ld kB (peak %ld)\n", autoFrames, am->finishedWon ? "WON" : "LOST", am->wavesCleared, (int)am->waves->waves.size(), am->lifes, autoMinFps == 999 ? -1 : autoMinFps, rss, peak);
					printf("DBG SOLDIERS sent %d  kills %d  damage %.0f  defeated %d  research %d  actions %d\n", am->stats.soldiersSent, am->stats.soldierKills, am->stats.soldierDamage, am->stats.enemiesDefeated, am->stats.researchDone, am->stats.specialsUsed);
					fflush(stdout);
					screen = next = S_QUIT;
					break;
				}
			}
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
		uint32_t tB = millis();
		if (next != screen)
			padPrev = padNow;   /* a press that changed the screen does not also act on the next one */
		screen = next;
		if (optShowFps && screen != S_LOADING)
		{
			char b[48];
			snprintf(b, sizeof b, "%dx%d @ %d fps", SW, SH, fps);
			draw_text(b, SW - 70, 3, 11, 0xFFFFFF, ALIGN_RIGHT, 2, 255, 0x000000);   /* left of TreeFrogUI's battery icon */
		}
		uint32_t tC = millis();
		music_update();
		uint32_t tD = millis();
		fpsFrames++;
		if (millis() - fpsStamp >= 1000)
		{
			fps = fpsFrames;
			fpsFrames = 0;
			fpsStamp = millis();
		}
		gfx_present();
		partsPlay = (int)(tB - tA);
		partsMusic = (int)(tD - tC);
		partsPresent = (int)(millis() - tD);
		uint32_t spent = millis() - now;
		if (spent < 25)
			SDL_Delay(25 - spent);
	}
	delete play.map;
	slotcheck_stop();
	save_settings();
	audio_shutdown();
	hwdisp_deinit();
	SDL_Quit();
	return 0;
}
