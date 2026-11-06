#include "game.hpp"
#include "audio.hpp"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float globalSpeed = 1.0f;
bool optShowFps = false, optHQ = false;
int camX = 0, camY = 0;

static const float PI_F = 3.14159265f;

/* The game's own clock. It runs at globalSpeed times real time (so the whole game, enemies, towers, arrows, spawns and
 * animations alike, speeds up with the fast button and stops when paused) and one frame never counts for more than
 * 100 ms, so a hiccup cannot make anything leap. Everything that happens in the match is timed with gnow(). */
static uint32_t g_gameMs = 1000, g_lastReal;
uint32_t gnow() { return g_gameMs; }
void game_clock_reset() { g_lastReal = millis(); }
static void game_clock_advance()
{
	uint32_t real = millis();
	uint32_t d = real - g_lastReal;
	g_lastReal = real;
	if (d > 100) d = 100;
	g_gameMs += (uint32_t)(d * globalSpeed + 0.5f);
}
static const uint32_t TEXT_GOLD = 0xFFF8C8;

static float dist(float x1, float y1, float x2, float y2) { float dx = x1 - x2, dy = y1 - y2; return sqrtf(dx * dx + dy * dy); }

/* one 64x64 (or whatever the sheet's tile is) frame, centred or by its corner, in world coordinates */
static bool frame_ok(const Sheet *s, int col, int row)
{
	return s && s->img.valid() && col >= 0 && row >= 0 && (s->tile <= 0 || ((col + 1) * s->tile <= s->img.w && (row + 1) * s->tile <= s->img.h));
}
class Tower;
struct DrawRec { const Sheet *s; int col, row; float cx, cy; const Tower *who; };
static const Tower *g_curTower = NULL;   /* the tower whose display() is running */
static DrawRec g_dr[96];
static int g_drn = 0, g_mapFrame = 0;   /* the ghost hunt: every centred sprite of the frame is noted */
static void frame_center(const Sheet *s, int col, int row, float cx, float cy, int alpha = 255, uint32_t tint = 0xFFFFFF)
{
	if (!frame_ok(s, col, row))
		return;
	if (g_drn < 96) { g_dr[g_drn].s = s; g_dr[g_drn].col = col; g_dr[g_drn].row = row; g_dr[g_drn].cx = cx; g_dr[g_drn].cy = cy; g_dr[g_drn].who = g_curTower; g_drn++; }
	draw_image_center(s->img, cx - camX, cy - camY, col * s->tile, row * s->tile, s->tile, s->tile, alpha, tint);
}
static void frame_corner(const Sheet *s, int col, int row, float x, float y, int alpha = 255, uint32_t tint = 0xFFFFFF)
{
	if (!frame_ok(s, col, row))
		return;
	draw_image(s->img, (int)(x - camX), (int)(y - camY), col * s->tile, row * s->tile, s->tile, s->tile, alpha, tint);
}

/* ------------------------------------------------------------------ char sets */
Sheet *CharSets::add(const std::string &file, int tile)
{
	std::map<std::string, Sheet>::iterator it = sets.find(file);
	if (it != sets.end())
		return &it->second;
	Sheet &s = sets[file];
	s.tile = tile;
	load_image(mapPath + "/data/" + file, s.img);
	return &s;
}
Sheet *CharSets::get(const std::string &file)
{
	std::map<std::string, Sheet>::iterator it = sets.find(file);
	return it == sets.end() ? NULL : &it->second;
}

/* ------------------------------------------------------------------ enemy */
/* What creatures do to towers: a fraction of the listed damage by difficulty (so Easy is gentle), and no tower loses more than a share of its health
 * per second to creatures, however many pass by. Both are in defensemania.cfg. */
float tower_damage_factor(int difficulty)
{
	static const char *key[3] = { "tower_damage_easy", "tower_damage_normal", "tower_damage_hard" };
	static const double def[3] = { 0.5, 0.75, 1.0 };
	int i = difficulty < 1 ? 0 : difficulty > 3 ? 2 : difficulty - 1;
	return (float)cfg_num(key[i], def[i]);
}
static float tower_damage_cap(int difficulty)
{
	static const char *key[3] = { "tower_damage_cap_easy", "tower_damage_cap_normal", "tower_damage_cap_hard" };
	static const double def[3] = { 6, 10, 15 };
	int i = difficulty < 1 ? 0 : difficulty > 3 ? 2 : difficulty - 1;
	return (float)cfg_num(key[i], def[i]);
}

Enemy::Enemy(const XNode &def, Map *m, const WaveScale &sc) : map(m)
{
	position[0] = position[1] = -16;
	moveVector[0] = moveVector[1] = 0;
	sheetFile = def.str("charset");
	tileSize = def.num("tilesize");
	sheet = map->charSets.add(sheetFile, tileSize);
	icon = map->charSets.add(def.str("icon"), -1);
	maxHealth = health = (int)(def.num("maxhealth") * map->difficulty * sc.hp + 0.5f);
	if (maxHealth < 1) maxHealth = health = 1;
	armor = def.flt("armor");
	kind = def.str("kind");
	thrower = def.num("thrower") != 0;
	throwRange = def.flt("throwrange", 150);
	throwDamage = def.flt("throwdamage", 100) * tower_damage_factor(map->difficulty) * sc.damage;
	liftTime = def.num("lifttime", 2500);
	if (def.num("attackrate") > 0) fireRate = def.num("attackrate");
	if (def.num("attackradius") > 0) fireRadius = def.num("attackradius");
	if (getenv("DM_ALLTHROW")) { thrower = true; liftTime = 1500; }   /* test hooks */
	if (getenv("DM_THROWRANGE")) throwRange = (float)atof(getenv("DM_THROWRANGE"));
	if (getenv("DM_ARMOR")) armor = (float)atof(getenv("DM_ARMOR"));
	flying = def.num("flying") != 0 || getenv("DM_ALLFLY") != NULL;   /* the test hook makes every creature fly */
	moveSpeed = (int)(def.num("movespeed") * sc.speed + 0.5f);
	money = (int)(def.num("money") * sc.money + 0.5f);
	goldValue = (float)def.num("money") * sc.money * sc.gold;
	damage = def.flt("damage") * tower_damage_factor(map->difficulty) * sc.damage;
	animationSpeed = (float)def.num("animationspeed");
	moveCount = gnow();
}

void Enemy::calcFuture(int timeMs, float out[2]) const
{
	float px = ((float)moveSpeed / 1000) * timeMs;
	out[0] = position[0] + moveVector[0] * freezeFactorTemp * px;
	out[1] = position[1] + moveVector[1] * freezeFactorTemp * px;
}

void Enemy::setWaypoints(const int *s, const int *e, const std::vector<std::pair<int, int> > &wps)
{
	start[0] = s[0]; start[1] = s[1];
	end[0] = e[0]; end[1] = e[1];
	way = wps;
	reachingEndpoint = false;
	position[1] = (float)start[1];
	position[0] = (float)(start[0] - tileSize);
	wpPosition = -1;
	moveCount = gnow();
	calculateMoveVector();
	/* the health bar sits just above the sprite's first visible row */
	if (healthBarOffset == -1 && frame_ok(sheet, 1, 0))
		for (int y = 0; y < sheet->tile && healthBarOffset == -1; y++)
			for (int x = 0; x < sheet->tile; x++)
				if (sheet->img.px[(size_t)y * sheet->img.w + sheet->tile + x] >> 24)
				{
					healthBarOffset = y - 1;
					break;
				}
	wayPointsSet = true;
	moveCount = gnow();
}

/* The way points may lie in any direction (The Swamp uses diagonals): the enemy walks straight at the next one. After
 * the last way point it carries on towards the end point and one tile beyond it, as in the original game. */
void Enemy::calculateMoveVector()
{
	wpPosition++;
	if (wpPosition < (int)way.size())
	{
		target[0] = (float)way[wpPosition].first;
		target[1] = (float)way[wpPosition].second;
	}
	else
	{
		float fromX = way.empty() ? (float)start[0] : (float)way.back().first, fromY = way.empty() ? (float)start[1] : (float)way.back().second;
		float dx = end[0] - fromX, dy = end[1] - fromY, len = sqrtf(dx * dx + dy * dy);
		if (len < 0.001f) { dx = 1; dy = 0; len = 1; }
		target[0] = end[0] + dx / len * tileSize;
		target[1] = end[1] + dy / len * tileSize;
		reachingEndpoint = true;
	}
	float dx = target[0] - position[0], dy = target[1] - position[1], len = sqrtf(dx * dx + dy * dy);
	if (len < 0.5f)
	{
		moveVector[0] = moveVector[1] = 0;
		return;
	}
	moveVector[0] = dx / len;
	moveVector[1] = dy / len;
	if (fabsf(dx) >= fabsf(dy)) moveDirection = dx > 0 ? 2 : 1;
	else moveDirection = dy > 0 ? 0 : 3;
}

void Enemy::checkWP()
{
	float dx = target[0] - position[0], dy = target[1] - position[1];
	if (dx * moveVector[0] + dy * moveVector[1] > 0)
		return;   /* still on the way */
	position[0] = target[0];
	position[1] = target[1];
	if (reachingEndpoint)
	{
		map->lifes--;
		map->stats.enemiesLeaked++;
		sfx_play(SFX_LIFELOST);
		if (map->lifes == 0)
			map->loseGame();
		enemyAlive = false;
		return;
	}
	calculateMoveVector();
}

void Enemy::display()
{
	int tile = map->tileSize;
	float drawX = position[0] - tileSize / 2;
	float drawY = position[1] - tileSize / 2 - ((tileSize / 2) - (tile / 2)) - (tile / 4);
	if (!enemyDied && !enemyAlive)
	{
		int alpha = 255;
		if (dieStep < 2)
		{
			if ((int)gnow() - dieCount > 100)
			{
				dieCount = gnow();
				dieStep++;
			}
		}
		else
		{
			dieTint -= 15;
			if (dieTint <= 15)
				enemyDied = true;
			alpha = dieTint < 0 ? 0 : dieTint;
		}
		frame_corner(sheet, dieStep, 4 + moveDirection, drawX, drawY, alpha);
	}
	if (wayPointsSet && enemyAlive)
	{
		uint32_t tint = 0xFFFFFF;
		freezeFactorTemp = 1;
		if (freezeCount > (int)gnow() - freezeTime)
		{
			freezeFactorTemp = freezeFactor;
			tint = 0x0099CC;
		}
		else
			frozen = false;
		if (!flying && (int)gnow() < glueUntil)
		{
			if (glueFactor < freezeFactorTemp) freezeFactorTemp = glueFactor;
			if (tint == 0xFFFFFF) tint = 0xCCDD66;
		}
		if ((int)gnow() < frostUntil)
		{
			if (frostFactor < freezeFactorTemp) freezeFactorTemp = frostFactor;
			tint = 0x88CCFF;
		}
		if (damage > 0 && (float)((int)gnow() - fireCount) >= (fireRate / freezeFactorTemp))
		{
			map->checkTowers((int)position[0], (int)position[1], fireRadius, damage);
			fireCount = gnow();
		}
		if ((float)((int)gnow() - animationCount) >= (1000 / (animationSpeed * freezeFactorTemp)))
		{
			moveState += moveStateDirection;
			if (moveState == 3 || moveState == -1)
			{
				moveState = 1;
				moveStateDirection *= -1;
			}
			animationCount = gnow();
		}
		checkWP();
		float dt = (float)((int)gnow() - moveCount);
		float px = ((float)moveSpeed / 1000) * dt;
		if (!flying && px > 0)
		{
			Trap *bt = map->blockingTrap(position[0], position[1], moveVector[0], moveVector[1], px * freezeFactorTemp);
			if (bt)   /* a boulder in the way: stop, and the first few creatures smash it */
			{
				px = 0;
				bool lifting = false;
				if (thrower)   /* a troll or ogre would rather lift it and throw it at a tower in reach */
				{
					int now = (int)gnow();
					if (liftTrapId != bt->id && map->towerInReach(bt->x, bt->y, throwRange))
					{
						liftTrapId = bt->id;
						liftStart = now;
					}
					if (liftTrapId == bt->id)
					{
						lifting = true;
						if (now - liftStart >= liftTime)
						{
							if (map->throwBoulder(bt, throwRange, throwDamage)) bt = NULL;
							liftTrapId = 0;
							lifting = bt != NULL ? false : true;
						}
					}
				}
				if (bt && !lifting && bt->attackers < map->trapTypes[bt->type].attackers)
				{
					bt->attackers++;
					bt->health -= (4.0f + maxHealth * 0.03f) * dt / 1000.0f;
				}
			}
			else if (map->blockingSoldier(position[0], position[1], moveVector[0], moveVector[1], px * freezeFactorTemp))
				px = 0;   /* a soldier holds the path: stop and fight (a few creatures at a time; the rest walk on) */
		}
		static int traced;   /* a few lines in log.txt if an enemy ever leaps, to help find out why */
		const bool trace = traced < 30;
		float oldX = position[0], oldY = position[1];
		position[0] += moveVector[0] * freezeFactorTemp * px;
		position[1] += moveVector[1] * freezeFactorTemp * px;
		if (trace)
		{
			float jump = dist(oldX, oldY, position[0], position[1]);
			if (jump > 40 + px * 1.2f)
			{
				traced++;
				printf("enemy jump: %p moved %.1f px (expected %.1f) in %.0f game ms, now at %.0f,%.0f, way point %d, speed x%.1f\n", (void *)this, jump, px, dt, position[0], position[1], wpPosition, globalSpeed);
				fflush(stdout);
			}
		}
		moveComplete += fabsf(moveVector[0] * freezeFactorTemp * px) + fabsf(moveVector[1] * freezeFactorTemp * px);
		moveCount = gnow();
		frame_corner(sheet, moveState, moveDirection, drawX, drawY, 255, tint);
	}
	if (gotHit)
	{
		Sheet *hs = map->charSets.get(hitCharset);
		int dt = (int)gnow() - hitCount, step = dt < 80 ? 0 : dt < 160 ? 1 : dt < 240 ? 2 : -1;
		if (step < 0)
			gotHit = false;
		else
			frame_center(hs, hitX + step, 1, position[0], position[1] - ((tileSize / 2) - (tile / 2)));
	}
}

void Enemy::displayGui()
{
	if (wayPointsSet && enemyAlive && health < maxHealth)
	{
		int tile = map->tileSize;
		float x = position[0] - tileSize / 2 + tileSize * 0.25f - camX;
		float y = position[1] - tileSize / 2 - tileSize * 0.25f - ((tileSize / 2) - (tile / 2)) + healthBarOffset - camY;
		float w = tileSize * 0.5f;
		fill_rect((int)x, (int)y, (int)w, 3, 0xCC0033);
		fill_rect((int)x, (int)y, (int)(w * ((float)health / (float)maxHealth)), 3, 0x00CC33);
		rect_outline((int)x - 1, (int)y - 1, (int)w + 2, 5, 0x323232);
	}
}

void Enemy::getDamage(float dmg, float freeze, float chill, float slowMs)
{
	int now = (int)gnow();
	if (freeze > 0)
	{
		freezeCount = now;
		freezeFactor = freeze;
		frozen = true;
		freezeTime = (int)slowMs;
		if (now >= chillUntil || chill > chillPct) chillPct = chill;   /* a slowed creature takes more damage from every tower for as long as the slow lasts */
		chillUntil = now + (int)slowMs;
	}
	if (now < chillUntil && chillPct > 0) dmg *= 1.0f + chillPct / 100.0f;
	if (armor > 0)
	{
		float floorDmg = dmg * 0.25f;   /* armour never stops a hit altogether */
		dmg = dmg - armor < floorDmg ? floorDmg : dmg - armor;
	}
	dmgCarry += dmg;   /* small hits (splash!) still add up: the fraction is kept for the next one */
	int whole = (int)dmgCarry;
	dmgCarry -= (float)whole;
	health -= whole;
	if (health < 0)
	{
		moveComplete = 999999999;
		enemyAlive = false;
		dieCount = gnow();
		float bonus = map->supportGold(position[0], position[1], goldValue);
		map->stats.goldEarned += map->award(goldValue + bonus);
		map->stats.depotGold += bonus;
		map->killPoints += money * 5;
		map->stats.enemiesDefeated++;
		sfx_play(SFX_DIE);
	}
}

void Enemy::hitByTower(float dmg, float freeze, float chill, float slowMs) { getDamage(dmg, freeze, chill, slowMs); }

void Enemy::hitByTower(float dmg, float freeze, Tower *tower, float multiplier, float chill, float slowMs)
{
	int hitDamage = (int)dmg;
	bool found = false;
	for (size_t i = 0; i < towerHits.size(); i++)
		if (towerHits[i].tower == tower)
		{
			found = true;
			hitDamage = towerHits[i].next();
			break;
		}
	if (!found)
	{
		TowerHit h = { tower, 1, (int)dmg, multiplier };
		towerHits.push_back(h);
	}
	getDamage((float)hitDamage, freeze, chill, slowMs);
}

void Enemy::hitAnimation(const std::string &cs, int x)
{
	gotHit = true;
	hitCount = gnow();
	hitX = x;
	hitCharset = cs;
}

/* The gold a kill pays: the money value of the creature, times a factor for the difficulty (Hard has three times the health of Easy, so it
 * pays a little more) and for how far into the match the wave is (late waves are far tougher than the first ones, and the gold has to
 * keep up with that). Both rates are in defensemania.cfg. */
static float gold_factor(int difficulty, int wave, int waves)
{
	float diff = 1.0f + (float)cfg_num("gold_difficulty_step", 0.10) * (difficulty - 1);
	float growth = waves > 1 ? (float)cfg_num("gold_wave_growth", 0.8) * wave / (float)(waves - 1) : 0.0f;
	return diff * (1.0f + growth);
}
/* ------------------------------------------------------------------ traps */
bool Map::nearPath(float wx, float wy, float maxDist, float &px, float &py) const
{
	float best = 1e30f;
	auto segment = [&](float ax, float ay, float bx, float by) {
		float vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
		float t = l2 > 0 ? ((wx - ax) * vx + (wy - ay) * vy) / l2 : 0;
		t = t < 0 ? 0 : t > 1 ? 1 : t;
		float qx = ax + vx * t, qy = ay + vy * t, d = dist(wx, wy, qx, qy);
		if (d < best) { best = d; px = qx; py = qy; }
	};
	for (size_t i = 0; i + 1 < pathPts.size(); i++)
		segment(pathPts[i].first, pathPts[i].second, pathPts[i + 1].first, pathPts[i + 1].second);
	for (size_t k = 0; k < detours.size(); k++)   /* a side road is part of the path too: traps can go there */
	{
		float ax = (float)way[detours[k].from - 1].first, ay = (float)way[detours[k].from - 1].second;
		for (size_t i = 0; i < detours[k].out.size(); i++)
		{
			float bx = (float)detours[k].out[i].first, by = (float)detours[k].out[i].second;
			segment(ax, ay, bx, by);
			ax = bx; ay = by;
		}
	}
	return best <= maxDist;
}

std::vector<std::pair<int, int> > Map::routeFor(bool flying) const
{
	if (detours.empty() || flying)
		return way;   /* creatures that fly go straight */
	static unsigned seed = 2463534242u;
	seed ^= (unsigned)gnow() * 2654435761u;
	std::vector<std::pair<int, int> > route;
	int d = difficulty < 1 ? 0 : difficulty > 3 ? 2 : difficulty - 1;
	for (size_t i = 0; i < way.size(); i++)
	{
		route.push_back(way[i]);
		for (size_t k = 0; k < detours.size(); k++)
			if (detours[k].from - 1 == (int)i)
			{
				seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;   /* xorshift: a fresh roll for every creature */
				if ((seed % 10000) / 100.0f < detours[k].chance[d])
				{
					const std::vector<std::pair<int, int> > &o = detours[k].out;
					for (size_t j = 0; j < o.size(); j++) route.push_back(o[j]);                       /* out to the dead end ... */
					for (size_t j = o.size() - 1; j-- > 0;) route.push_back(o[j]);                     /* ... and back the way it came */
					route.push_back(way[i]);                                                           /* to the junction, then on */
				}
			}
	}
	static const bool log = getenv("DM_ROUTELOG") != NULL;   /* test hook: says how many creatures took a side road */
	if (log) { printf("ROUTE difficulty %d: %s\n", difficulty, route.size() > way.size() ? "detour" : "straight"); fflush(stdout); }
	return route;
}

int Map::trapCount(int type) const
{
	int n = 0;
	for (size_t i = 0; i < traps.size(); i++)
		if (traps[i].type == type) n++;
	return n;
}

std::string Map::trapProblem(int type, float px, float py) const
{
	if (type < 0 || type >= (int)trapTypes.size()) return "No such trap";
	const TrapType &t = trapTypes[type];
	if (trapCount(type) >= trapCap(type)) return "You can have no more of these";
	if (money < t.costs) return "Not enough gold";
	if (pathPts.size() >= 2 && (dist(px, py, pathPts.front().first, pathPts.front().second) < 90 || dist(px, py, pathPts.back().first, pathPts.back().second) < 90))
		return "Too close to the start or the end";
	for (size_t i = 0; i < traps.size(); i++)
		if (dist(px, py, traps[i].x, traps[i].y) < 44) return "Too close to another trap";
	return "";
}

bool Map::addTrap(int type, float px, float py)
{
	if (!trapProblem(type, px, py).empty())
		return false;
	const TrapType &t = trapTypes[type];
	money -= t.costs;
	stats.goldSpent += t.costs;
	if (type < 4) stats.trapsBuilt[type]++;
	Trap tr;
	tr.id = ++trapSerial; tr.type = type; tr.x = px; tr.y = py; tr.born = (int)gnow();
	const TrapMods &tm = trapMods[type];
	tr.charges = (float)t.charges * tm.charges; tr.maxCharges = tr.charges; tr.health = (float)t.health * tm.health; tr.maxHealth = tr.health;
	tr.damage = t.damage * tm.damage; tr.slow = t.slow - tm.slow < 0.2f ? 0.2f : t.slow - tm.slow; tr.duration = (int)(t.duration * tm.duration);
	traps.push_back(tr);
	sfx_play(SFX_BUILD);
	return true;
}

/* the first boulder in the way of a creature that is about to move step pixels along (mx, my): it stops in front of it */
Trap *Map::blockingTrap(float x, float y, float mx, float my, float step)
{
	for (size_t i = 0; i < traps.size(); i++)
	{
		Trap &t = traps[i];
		if (trapTypes[t.type].kind != "boulder" || t.health <= 0) continue;
		float r = trapTypes[t.type].radius;
		float tx = t.x - x, ty = t.y - y;
		float ahead = tx * mx + ty * my;                   /* how far ahead along its direction the boulder is */
		if (ahead <= 0) continue;                           /* already past it */
		float side = fabsf(tx * my - ty * mx);              /* how far off to the side */
		if (side < r && ahead - r <= step + 1) return &t;   /* its next step reaches the boulder */
	}
	return NULL;
}

bool Map::towerInReach(float x, float y, float range) const
{
	for (size_t i = 0; i < towers.size(); i++)
		if (!towers[i]->destroyed && dist((towers[i]->position[0] + 0.5f) * tileSize, (towers[i]->position[1] + 0.5f) * tileSize, x, y) <= range) return true;
	return false;
}

/* a creature has lifted the boulder: it flies at the nearest tower in reach (ties: the one built first), which takes the hit when it lands */
bool Map::throwBoulder(Trap *t, float range, float damage)
{
	int best = -1;
	float bestD = 1e30f;
	for (size_t i = 0; i < towers.size(); i++)
	{
		if (towers[i]->destroyed) continue;
		float d = dist((towers[i]->position[0] + 0.5f) * tileSize, (towers[i]->position[1] + 0.5f) * tileSize, t->x, t->y);
		if (d <= range && d < bestD) { bestD = d; best = (int)i; }
	}
	if (best < 0) return false;
	Projectile p;
	p.x0 = t->x; p.y0 = t->y;
	p.x1 = (towers[best]->position[0] + 0.5f) * tileSize; p.y1 = (towers[best]->position[1] + 0.5f) * tileSize;
	p.start = (int)gnow(); p.duration = 450; p.damage = damage;
	projectiles.push_back(p);
	stats.boulderThrows++;
	if (getenv("DM_TRAPLOG")) { printf("THROW boulder %d at tower %d,%d for %.0f\n", t->id, towers[best]->position[0], towers[best]->position[1], damage); fflush(stdout); }
	t->health = 0;   /* the boulder is gone from the path */
	sfx_play(SFX_CATAPULT);
	return true;
}

bool Map::hasThrowers() const
{
	if (!waves) return false;
	for (size_t i = 0; i < waves->waves.size(); i++)
		for (size_t k = 0; k < waves->waves[i]->list.size(); k++)
			if (waves->waves[i]->list[k]->thrower) return true;
	return false;
}

void Map::updateTraps()
{
	int now = (int)gnow();
	for (size_t i = 0; i < traps.size(); i++) traps[i].attackers = 0;
	if (getenv("DM_TRAPLOG")) { static int last; if (now - last > 5000) { last = now; for (size_t i = 0; i < traps.size(); i++) printf("TRAPLOG %s health %.0f charges %.0f at game time %d\n", trapTypes[traps[i].type].kind.c_str(), traps[i].health, traps[i].charges, now); fflush(stdout); } }
	if (!traps.empty() && waves && waves->started)
	{
		Wave *w = waves->waves[waves->current];
		for (size_t i = 0; i < traps.size(); i++)
		{
			Trap &t = traps[i];
			const TrapType &tt = trapTypes[t.type];
			if (tt.kind == "boulder") continue;
			for (size_t k = 0; k < w->list.size() && t.charges > 0; k++)
			{
				Enemy *e = w->list[k];
				if (!e->alive() || e->flying || e->position[0] <= 0 || dist(e->position[0], e->position[1], t.x, t.y) > tt.radius) continue;
				if (tt.kind == "glue")
				{
					e->glueFactor = now >= e->glueUntil ? t.slow : (t.slow < e->glueFactor ? t.slow : e->glueFactor);
					e->glueUntil = now + 250;
				}
				else if (tt.kind == "spikes")
				{
					int last = -100000;
					size_t h = 0;
					for (; h < t.hits.size(); h++) if (t.hits[h].first == e) { last = t.hits[h].second; break; }
					if (now - last >= tt.interval)
					{
						e->hitByTower(t.damage, 0);
						t.charges -= 1;
						if (h < t.hits.size()) t.hits[h].second = now; else t.hits.push_back(std::make_pair(e, now));
					}
				}
			}
			if (t.hits.size() > 64) t.hits.erase(t.hits.begin(), t.hits.begin() + 32);
		}
	}
	for (size_t i = 0; i < projectiles.size();)
	{
		const Projectile &p = projectiles[i];
		if (now - p.start >= p.duration)
		{
			for (size_t k = 0; k < towers.size(); k++)   /* the tower that stands where it lands */
				if (!towers[k]->destroyed && dist((towers[k]->position[0] + 0.5f) * tileSize, (towers[k]->position[1] + 0.5f) * tileSize, p.x1, p.y1) < 4)
					towers[k]->enemyDamage(p.damage);
			sfx_play(SFX_CRUMBLE);
			projectiles.erase(projectiles.begin() + i);
		}
		else i++;
	}
	for (size_t i = 0; i < traps.size();)
	{
		Trap &t = traps[i];
		const TrapType &tt = trapTypes[t.type];
		bool gone = (tt.kind == "spikes" && t.charges <= 0) || (tt.kind == "glue" && now - t.born > t.duration) || (tt.kind == "boulder" && t.health <= 0);
		if (gone) traps.erase(traps.begin() + i); else i++;
	}
}

void Map::drawTraps()
{
	int now = (int)gnow();
	for (size_t i = 0; i < projectiles.size(); i++)
	{
		const Projectile &p = projectiles[i];
		float t = (float)(now - p.start) / p.duration;
		t = t < 0 ? 0 : t > 1 ? 1 : t;
		float x = p.x0 + (p.x1 - p.x0) * t, y = p.y0 + (p.y1 - p.y0) * t - sinf(t * PI_F) * 60;
		for (size_t k = 0; k < trapTypes.size(); k++)
			if (trapTypes[k].kind == "boulder") draw_image_center(trapTypes[k].picture, x - camX, y - camY, 0, 0, -1, -1, 255, 0xFFFFFF);
	}
	for (size_t i = 0; i < traps.size(); i++)
	{
		const Trap &t = traps[i];
		const TrapType &tt = trapTypes[t.type];
		int alpha = 255;
		if (tt.kind == "glue")
		{
			int left = t.duration - (now - t.born);
			alpha = left < 4000 ? 90 + left * 165 / 4000 : 255;   /* it dries up */
		}
		draw_image_center(tt.picture, t.x - camX, t.y - camY, 0, 0, -1, -1, alpha, 0xFFFFFF);
		if (tt.kind == "boulder" && t.maxHealth > 0)
		{
			if (t.health < t.maxHealth * 0.66f && tt.crack[0].valid()) draw_image_center(tt.crack[0], t.x - camX, t.y - camY);
			if (t.health < t.maxHealth * 0.33f && tt.crack[1].valid()) draw_image_center(tt.crack[1], t.x - camX, t.y - camY);
		}
		if (tt.kind == "boulder" && t.health < t.maxHealth)
		{
			int w = 30, x = (int)(t.x - camX) - w / 2, y = (int)(t.y - camY) - 28;
			fill_rect(x, y, w, 3, 0xCC0033);
			fill_rect(x, y, (int)(w * (t.health > 0 ? t.health : 0) / t.maxHealth), 3, 0x00CC33);
			rect_outline(x - 1, y - 1, w + 2, 5, 0x323232);
		}
		else if (tt.kind == "spikes" && t.charges < t.maxCharges / 3)
			fill_rect((int)(t.x - camX) - 8, (int)(t.y - camY) - 14, 16, 2, 0xFFAA33);   /* nearly worn out */
	}
}

float Tower::slowFactor() const
{
	if (freeze <= 0) return 0;
	float f = freeze - map->towerMods[typeIdx].slow;
	return f < 0.2f ? 0.2f : f;
}
float Tower::slowMs() const { return 2500.0f * map->towerMods[typeIdx].slowtime; }

/* ------------------------------------------------------------------ the Bastion: notices, research, specials */
void Map::pushNotice(const std::string &s) { noticeQueue.push_back(s); }

void Map::updateNotices()
{
	int now = (int)millis();
	if (now >= noticeUntil && !noticeQueue.empty())
	{
		notice = noticeQueue.front();
		noticeQueue.erase(noticeQueue.begin());
		noticeUntil = now + 3500;
	}
}

bool Map::nearBastion(float wx, float wy) const { return fabsf(wx - keepX) <= 38 && wy >= keepY - 128 && wy <= keepY + 8; }
bool Map::keepBlocks(int wx, int wy) const { return fabsf(wx - keepX) <= 40 && wy >= keepY - 34 && wy <= keepY + 6; }

/* The keep is 64 px wide and 128 tall (two of the towers' tiles). It stands beside the end of the path, as near as it can without covering any of the
   path (the creatures and soldiers are drawn over the keep, so a path hidden behind it would be walked on air), on either side of the last stretch. */
void Map::placeKeep()
{
	float px = pathPts.size() >= 2 ? pathPts[pathPts.size() - 2].first : bastionX - 1, py = pathPts.size() >= 2 ? pathPts[pathPts.size() - 2].second : bastionY;
	float ux = bastionX - px, uy = bastionY - py, len = sqrtf(ux * ux + uy * uy);
	if (len < 1) { ux = 1; uy = 0; len = 1; }
	ux /= len; uy /= len;
	float bestCost = 1e30f;
	keepX = bastionX; keepY = bastionY;
	for (int side = 0; side < 2; side++)
	{
		float nx = side == 0 ? uy : -uy, ny = side == 0 ? -ux : ux;   /* the two sides of the path; the first is the left, up on a path that runs right */
		float base = 16 + 32 * fabsf(nx) + 24 * (ny > 0 ? ny : 0);
		for (float extra = 0; extra <= 300; extra += 6)
		{
			float fx = bastionX + nx * (base + extra) - ux * 20, fy = bastionY + ny * (base + extra) - uy * 20;
			if (fx < 34) fx = 34;
			if (fx > mapW - 34) fx = (float)mapW - 34;
			if (fy < 128) fy = 128;
			if (fy > mapH - 2) fy = (float)mapH - 2;
			bool clear = true;
			for (float sy = fy - 128; sy <= fy && clear; sy += 12)
				for (float sx = fx - 34; sx <= fx + 34 && clear; sx += 12)
				{
					float qx, qy;
					if (nearPath(sx, sy, 16, qx, qy)) clear = false;
				}
			float cost = dist(fx, fy - 40, bastionX, bastionY) + (side == 1 ? 4 : 0);
			if (clear && cost < bestCost) { bestCost = cost; keepX = fx; keepY = fy; break; }
		}
	}
}

int Map::trapCap(int type) const
{
	if (type < 0 || type >= (int)trapTypes.size()) return 0;
	return trapTypes[type].cap + (type < (int)trapMods.size() ? trapMods[type].cap : 0);
}

int Map::specialLine(const char *id) const
{
	for (size_t i = 0; i < research.size(); i++)
		if (research[i].id == id) return (int)i;
	return -1;
}
int Map::specialLevel(const char *id) const
{
	int l = specialLine(id);
	return l < 0 ? 0 : research[l].done;
}

std::string Map::researchProblem(int line) const
{
	if (line < 0 || line >= (int)research.size()) return "No such research";
	const ResearchLine &r = research[line];
	if (researchActive >= 0) return "Another project is under way";
	if (r.done >= (int)r.tier.size()) return "Fully researched";
	if (money < r.tier[r.done].cost) return "Not enough gold";
	return "";
}

bool Map::startResearch(int line)
{
	if (!researchProblem(line).empty()) return false;
	const ResearchTier &t = research[line].tier[research[line].done];
	money -= t.cost;
	stats.goldSpent += t.cost;
	researchActive = line;
	researchLeft = t.waves;
	researchSkipNow = waves && waves->started && waves->current < (int)waves->waves.size() && waves->waves[waves->current]->begun();
	sfx_play(SFX_BUILD);
	return true;
}

void Map::researchWaveDone()
{
	if (researchActive < 0) return;
	if (researchSkipNow) { researchSkipNow = false; return; }   /* the wave that was already on when it was started does not count */
	if (--researchLeft > 0) return;
	ResearchLine &r = research[researchActive];
	const ResearchTier &t = r.tier[r.done];
	r.done++;
	researchActive = -1;
	stats.researchDone++;
	applyResearch();
	pushNotice("RESEARCH COMPLETE:  " + r.title + " - " + t.name);
	sfx_play(SFX_UPGRADE);
}

/* the finished tiers, added up into the multipliers the towers and traps use */
void Map::applyResearch()
{
	towerMods.assign(towerTypes.size(), TowerMods());
	trapMods.assign(trapTypes.size(), TrapMods());
	int lives = 0;
	for (size_t i = 0; i < research.size(); i++)
	{
		const ResearchLine &r = research[i];
		for (int k = 0; k < r.done && k < (int)r.tier.size(); k++)
		{
			const ResearchTier &t = r.tier[k];
			if (r.group == "tower")
			{
				for (size_t ti = 0; ti < towerTypes.size(); ti++)
					if (towerTypes[ti].name == r.target)
					{
						TowerMods &m = towerMods[ti];
						m.damage += t.get("damage") / 100; m.rate += t.get("rate") / 100; m.reach += t.get("reach") / 100; m.splash += t.get("splash") / 100;
						m.slowtime += t.get("slowtime") / 100; m.suprange += t.get("suprange") / 100;
						m.chill += t.get("chill"); m.slow += t.get("slow"); m.ramp += t.get("ramp"); m.supheal += t.get("supheal"); m.supgold += t.get("supgold");
					}
			}
			else if (r.group == "trap")
			{
				for (size_t ti = 0; ti < trapTypes.size(); ti++)
					if (trapTypes[ti].id == r.target)
					{
						TrapMods &m = trapMods[ti];
						m.damage += t.get("trapdamage") / 100; m.charges += t.get("trapcharges") / 100; m.duration += t.get("trapduration") / 100;
						m.health += t.get("traphealth") / 100; m.slow += t.get("trapslow"); m.cap += (int)t.get("trapcap");
					}
			}
			else if (r.id == "fortify")
				lives += (int)t.get("lives");
			else if (r.group == "garrison")
				;   /* read when the soldiers go out: spawnSoldiers */
		}
	}
	if (lives > extraLives) { lifes += lives - extraLives; extraLives = lives; }   /* the extra lives are added once */
	/* doctrines: 3, 6 and 9 tiers finished in a path pay a bonus to everything in it */
	int lt = doctrineLevel(0), lp = doctrineLevel(1), lg = doctrineLevel(2), lc = doctrineLevel(3);
	for (size_t ti = 0; ti < towerMods.size(); ti++) { towerMods[ti].damage += 0.04f * lt; towerMods[ti].rate += 0.04f * lt; }
	for (size_t ti = 0; ti < trapMods.size(); ti++) { trapMods[ti].damage += 0.06f * lp; trapMods[ti].charges += 0.06f * lp; trapMods[ti].health += 0.06f * lp; }
	soldierHpMul = 1 + 0.06f * lg; soldierDmgMul = 1 + 0.06f * lg; garrisonCd = 1 - 0.06f * lg; commandCd = 1 - 0.08f * lc;
	static const char *pathName[4] = { "Towers", "Traps", "Garrison", "Command" };
	static const char *pathText[4] = { "towers +4% damage and fire rate", "traps +6% damage, charges and health", "soldiers +6% health and damage, 6% shorter waits", "actions wait 8% less" };
	int levels[4] = { lt, lp, lg, lc };
	for (int c = 0; c < 4; c++)
		if (levels[c] > doctrineSeen[c])
		{
			doctrineSeen[c] = levels[c];
			char b[160];
			snprintf(b, sizeof b, "DOCTRINE %d:  %s - %s (each level)", levels[c], pathName[c], pathText[c]);
			pushNotice(b);
		}
}

/* the Bastion's actions: 0 Volley, 1 Rally, 2 Mend, 3 Frost Horn, then the soldiers: 4 Vermin Hunters, 5 Giant Slayers, 6 Skyguard, 7 Boulder Rollers */
static const char *kSpecialId[8] = { "volley", "rally", "mend", "frost", "vermin", "giant", "sky", "roller" };

std::string Map::specialProblem(int idx) const
{
	if (idx < 0 || idx > 7) return "No such action";
	if (specialLevel(kSpecialId[idx]) <= 0) return "Not researched yet";
	int left = specialReadyAt[idx] - (int)gnow();
	if (left > 0) { char b[48]; snprintf(b, sizeof b, "Ready in %d s", (left + 999) / 1000); return b; }
	if (defeated || !waves || !waves->started || waves->current >= (int)waves->waves.size() || !waves->waves[waves->current]->begun()) return "Only while a wave is on";
	return "";
}

bool Map::useSpecial(int idx)
{
	if (!specialProblem(idx).empty()) return false;
	const ResearchLine &r = research[specialLine(kSpecialId[idx])];
	const ResearchTier &t = r.tier[r.done - 1];
	int now = (int)gnow();
	float power = t.get("power"), time = t.get("time");
	announced[idx] = false;
	specialReadyAt[idx] = now + (int)(t.get("cooldown") * 1000 * (idx >= 4 ? garrisonCd : commandCd));
	stats.specialsUsed++;
	if (idx >= 4)
	{
		spawnSoldiers(idx - 4, t);
		return true;
	}
	if (idx == 0)   /* Volley: every creature near the keep takes a share of its health */
	{
		Wave *w = waves->waves[waves->current];
		for (size_t k = 0; k < w->list.size(); k++)
		{
			Enemy *e = w->list[k];
			if (!e->alive() || e->position[0] <= 0 || dist(e->position[0], e->position[1], bastionX, bastionY) > 220) continue;
			float d = e->maxHealthValue() * power / 100.0f;
			e->hitByTower(d < 20 ? 20 : d, 0);
		}
		volleyUntil = (int)millis() + 700;
		sfx_play(SFX_ARROW);
		pushNotice("VOLLEY!");
	}
	else if (idx == 1)   /* Rally */
	{
		rallyUntil = now + (int)(time * 1000);
		rallyPower = power;
		sfx_play(SFX_UPGRADE);
		pushNotice("RALLY!  towers fire faster");
	}
	else if (idx == 2)   /* Mend */
	{
		for (size_t i = 0; i < towers.size(); i++)
			if (!towers[i]->destroyed)
			{
				int h = towers[i]->health + (int)(towers[i]->maxHealth * power / 100.0f);
				towers[i]->health = h > towers[i]->maxHealth ? towers[i]->maxHealth : h;
			}
		sfx_play(SFX_REPAIR);
		pushNotice("MEND!  towers repaired");
	}
	else   /* Frost Horn: every creature, flyers too */
	{
		Wave *w = waves->waves[waves->current];
		for (size_t k = 0; k < w->list.size(); k++)
		{
			Enemy *e = w->list[k];
			if (!e->alive()) continue;
			e->frostUntil = now + (int)(time * 1000);
			e->frostFactor = power / 100.0f;
		}
		sfx_play(SFX_FREEZE);
		pushNotice("FROST HORN!  everything slows");
	}
	return true;
}

/* ------------------------------------------------------------------ the garrison: soldiers sent out from the keep */
int Map::pathTiers(int cat) const
{
	int n = 0;
	for (size_t i = 0; i < research.size(); i++)
	{
		const std::string &g = research[i].group;
		int c = g == "tower" ? 0 : g == "trap" ? 1 : g == "garrison" ? 2 : (research[i].id == "fortify" ? -1 : 3);
		if (c == cat) n += research[i].done;
	}
	return n;
}

void Map::ensurePathLen()
{
	if (pathCum.size() == pathPts.size()) return;
	pathCum.assign(pathPts.size(), 0.0f);
	for (size_t i = 1; i < pathPts.size(); i++)
		pathCum[i] = pathCum[i - 1] + dist(pathPts[i - 1].first, pathPts[i - 1].second, pathPts[i].first, pathPts[i].second);
}
float Map::pathLength() { ensurePathLen(); return pathCum.empty() ? 0.0f : pathCum.back(); }

void Map::pathAt(float sd, float &x, float &y, float &dx, float &dy)
{
	ensurePathLen();
	if (pathPts.size() < 2) { x = y = 0; dx = 1; dy = 0; return; }
	size_t i = 0;
	while (i + 2 < pathPts.size() && pathCum[i + 1] < sd) i++;
	float l = pathCum[i + 1] - pathCum[i];
	float t = l > 0 ? (sd - pathCum[i]) / l : 0;
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	x = pathPts[i].first + (pathPts[i + 1].first - pathPts[i].first) * t;
	y = pathPts[i].second + (pathPts[i + 1].second - pathPts[i].second) * t;
	dx = l > 0 ? (pathPts[i + 1].first - pathPts[i].first) / l : 1;
	dy = l > 0 ? (pathPts[i + 1].second - pathPts[i].second) / l : 0;
}

float Map::pathS(float wx, float wy)
{
	ensurePathLen();
	float best = 1e30f, bs = 0;
	for (size_t i = 0; i + 1 < pathPts.size(); i++)
	{
		float ax = pathPts[i].first, ay = pathPts[i].second, bx = pathPts[i + 1].first, by = pathPts[i + 1].second;
		float vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
		float t = l2 > 0 ? ((wx - ax) * vx + (wy - ay) * vy) / l2 : 0;
		t = t < 0 ? 0 : t > 1 ? 1 : t;
		float d = dist(wx, wy, ax + vx * t, ay + vy * t);
		if (d < best) { best = d; bs = pathCum[i] + sqrtf(l2) * t; }
	}
	return bs;
}

static bool soldier_vs(const SoldierType &st, const Enemy *e)
{
	for (size_t i = 0; i < st.vs.size(); i++) if (st.vs[i] == e->kind) return true;
	return false;
}

void Map::spawnSoldiers(int type, const ResearchTier &t)
{
	if (type < 0 || type >= (int)soldierTypes.size() || !waves) return;
	const SoldierType &st = soldierTypes[type];
	float len = pathLength();
	int count = (int)t.get("count", 1), now = (int)gnow();
	for (int i = 0; i < count; i++)
	{
		Soldier sd;
		sd.type = type;
		sd.born = now;
		sd.life = st.roller ? 100000000 : (int)(t.get("time", 30) * 1000);
		sd.maxHealth = sd.health = st.health * (1 + t.get("health") / 100) * soldierHpMul;
		sd.damage = st.damage * (1 + t.get("damage") / 100) * soldierDmgMul;
		sd.zone = t.get("zone", 450);
		sd.s = len - 50 - i * (st.roller ? 60.0f : 18.0f);
		if (sd.s < 40) sd.s = 40;
		pathAt(sd.s, sd.x, sd.y, sd.dirX, sd.dirY);
		sd.nextHit = now + 400;
		soldiers.push_back(sd);
		stats.soldiersSent++;
	}
	sfx_play(SFX_GO);
	pushNotice(std::string(st.roller ? "ROLL!  " : "TO ARMS!  ") + st.name + (count > 1 ? "s" : "") + " leave the keep");
}

Soldier *Map::blockingSoldier(float x, float y, float mx, float my, float step)
{
	for (size_t i = 0; i < soldiers.size(); i++)
	{
		Soldier &sd = soldiers[i];
		if (soldierTypes[sd.type].roller || sd.health <= 0 || sd.blockers >= 3) continue;
		float tx = sd.x - x, ty = sd.y - y;
		float ahead = tx * mx + ty * my, side = fabsf(tx * my - ty * mx);
		if (ahead > 0 && side < 18 && ahead - 20 <= step + 1) { sd.blockers++; return &sd; }
	}
	return NULL;
}

void Map::soldierDamage(float x, float y, float radius, float dmg)
{
	if (soldiers.empty()) return;
	int now = (int)gnow();
	for (size_t i = 0; i < soldiers.size(); i++)
	{
		Soldier &sd = soldiers[i];
		if (soldierTypes[sd.type].roller || sd.health <= 0 || dist(sd.x, sd.y, x, y) > radius) continue;
		if (now - sd.capStart >= 1000) { sd.capStart = now; sd.capTaken = 0; }
		float room = sd.maxHealth * (float)cfg_num("soldier_damage_cap", 15) / 100.0f - sd.capTaken;   /* a swarm cannot cut a soldier down in an instant */
		if (room <= 0) continue;
		float d = dmg > room ? room : dmg;
		sd.capTaken += d;
		sd.carry += d;
		int whole = (int)sd.carry;
		sd.carry -= (float)whole;
		sd.health -= (float)whole;
	}
}

/* a row of a sheet (the monsters' order: 0 down, 1 left, 2 right, 3 up) for a way of looking */
static int dir_row(float dx, float dy)
{
	if (fabsf(dx) >= fabsf(dy)) return dx > 0 ? 2 : 1;
	return dy > 0 ? 0 : 3;
}

void Map::updateSoldiers()
{
	if (soldiers.empty()) return;
	int now = (int)gnow();
	int dt = soldierTick < 0 ? 0 : now - soldierTick;
	soldierTick = now;
	if (dt < 0 || dt > 500) dt = 0;
	if (!waves) return;
	Wave *w = waves->waves[waves->current];
	if (waves->current != soldierWave)
	{
		soldierWave = waves->current;   /* the creatures of the last wave are gone: forget them */
		for (size_t i = 0; i < soldiers.size(); i++) { soldiers[i].target = NULL; soldiers[i].hits.clear(); soldiers[i].retarget = 0; }
	}
	float len = pathLength();
	for (size_t i = 0; i < soldiers.size(); i++) soldiers[i].blockers = 0;
	for (size_t i = 0; i < soldiers.size();)
	{
		Soldier &sd = soldiers[i];
		const SoldierType &st = soldierTypes[sd.type];
		bool gone = sd.health <= 0 || now - sd.born >= sd.life;
		if (!gone && st.roller)
		{
			sd.s -= st.speed * dt / 1000.0f;   /* up the path, against the stream */
			sd.moving = true;
			if (sd.s < 60) gone = true;
			pathAt(sd.s, sd.x, sd.y, sd.dirX, sd.dirY);
			sd.face = dir_row(-sd.dirX, -sd.dirY);
			for (size_t k = 0; k < w->list.size() && !gone; k++)
			{
				Enemy *e = w->list[k];
				if (!e->alive() || e->flying || e->position[0] <= 0 || dist(e->position[0], e->position[1], sd.x, sd.y) > st.reach) continue;
				int last = -100000;
				size_t h = 0;
				for (; h < sd.hits.size(); h++) if (sd.hits[h].first == e) { last = sd.hits[h].second; break; }
				if (now - last < (int)st.rate) continue;
				if (h < sd.hits.size()) sd.hits[h].second = now; else sd.hits.push_back(std::make_pair(e, now));
				float rd = sd.damage;
				float rshare = e->maxHealthValue() * st.pct / 100.0f * (sd.damage / st.damage);
				if (rshare > rd) rd = rshare;
				stats.soldierDamage += rd;
				sd.dealt += rd; sd.nhits++;
				e->hitByTower(rd, 0);
				if (!e->alive()) stats.soldierKills++;
				sd.strikeAt = now;
				sd.health -= st.wear + st.wearpct / 100.0f * e->maxHealthValue();   /* the boulder wears with every creature it hits */
				sfx_play(SFX_CRUMBLE);
			}
			if (sd.hits.size() > 96) sd.hits.erase(sd.hits.begin(), sd.hits.begin() + 48);
		}
		else if (!gone)
		{
			if (now >= sd.retarget || !sd.target || !sd.target->alive())
			{
				sd.retarget = now + 250;
				sd.target = NULL;
				float best = -1;
				for (size_t k = 0; k < w->list.size(); k++)   /* the creature of its own kind that is furthest along the path, nearest the keep */
				{
					Enemy *e = w->list[k];
					if (!e->alive() || e->position[0] <= 0 || (e->flying && !st.ranged) || !soldier_vs(st, e)) continue;
					float es = pathS(e->position[0], e->position[1]);
					if (es < len - sd.zone) continue;   /* beyond the stretch it guards: the towers' business */
					if (es > best) { best = es; sd.target = e; }
				}
			}
			sd.moving = false;
			if (sd.target)
			{
				float d = dist(sd.x, sd.y, sd.target->position[0], sd.target->position[1]);
				float es = pathS(sd.target->position[0], sd.target->position[1]);
				if (d > st.reach * 0.85f && fabsf(es - sd.s) > 3)
				{
					sd.s += (es < sd.s ? -1.0f : 1.0f) * st.speed * dt / 1000.0f;
					float lo = len - sd.zone > 60 ? len - sd.zone : 60;
					sd.s = sd.s < lo ? lo : sd.s > len - 30 ? len - 30 : sd.s;
					pathAt(sd.s, sd.x, sd.y, sd.dirX, sd.dirY);
					sd.moving = true;
					sd.face = dir_row((es < sd.s ? -1.0f : 1.0f) * sd.dirX, (es < sd.s ? -1.0f : 1.0f) * sd.dirY);
				}
				else if (d > 1) sd.face = dir_row(sd.target->position[0] - sd.x, sd.target->position[1] - sd.y);
			}
			if (now >= sd.nextHit)
			{
				Enemy *tgt = NULL;
				bool on = false;
				float bestScore = 1e30f;
				for (size_t k = 0; k < w->list.size(); k++)   /* in reach: its own kind first, then the nearest of anything else it can touch */
				{
					Enemy *e = w->list[k];
					if (!e->alive() || e->position[0] <= 0 || (e->flying && !st.ranged)) continue;
					float d = dist(sd.x, sd.y, e->position[0], e->position[1]);
					if (d > st.reach) continue;
					bool o = soldier_vs(st, e);
					float score = d - (o ? 10000.0f : 0.0f);
					if (score < bestScore) { bestScore = score; tgt = e; on = o; }
				}
				if (tgt)
				{
					float dmg = sd.damage * (on ? 1.0f : soldierOff);
					if (on)
					{
						float share = tgt->maxHealthValue() * st.pct / 100.0f * (sd.damage / st.damage);
						if (share > dmg) dmg = share;
						if (st.pierce) dmg += tgt->armor;
					}
					stats.soldierDamage += dmg;
					sd.dealt += dmg; sd.nhits++;
					tgt->hitByTower(dmg, 0);
					if (!tgt->alive()) stats.soldierKills++;
					sd.nextHit = now + (int)st.rate;
					sd.strikeAt = now;
					sd.face = dir_row(tgt->position[0] - sd.x, tgt->position[1] - sd.y);
					sd.flashX = tgt->position[0]; sd.flashY = tgt->position[1];
					sd.flashUntil = (int)millis() + 140;
					if (st.ranged) sfx_play(SFX_ARROW);
				}
				else sd.nextHit = now + 100;
			}
		}
		if (getenv("DM_SOLDLOG"))
		{
			static int lastLog;
			if (now - lastLog > 1500 && i == 0)
			{
				lastLog = now;
				float nearest = 1e9f; int onKind = 0;
				for (size_t k = 0; k < w->list.size(); k++) { Enemy *e = w->list[k]; if (!e->alive() || e->position[0] <= 0) continue; if (soldier_vs(st, e)) onKind++; float d = dist(sd.x, sd.y, e->position[0], e->position[1]); if (d < nearest) nearest = d; }
				printf("SOLDLOG t=%d type %s n=%d s=%.0f/%.0f pos %.0f,%.0f hp %.0f/%.0f target %s onkind alive %d nearest %.0f age %d/%d\n", now, st.id.c_str(), (int)soldiers.size(), sd.s, len, sd.x, sd.y, sd.health, sd.maxHealth, sd.target ? "yes" : "no", onKind, nearest, now - sd.born, sd.life);
				fflush(stdout);
			}
		}
		if (gone)
		{
			if (getenv("DM_SOLDLOG")) { printf("SOLDEND %s %s age %d/%d hits %d dealt %.0f hp %.0f/%.0f wave %d\n", st.id.c_str(), sd.health <= 0 ? "KILLED" : "time", now - sd.born, sd.life, sd.nhits, sd.dealt, sd.health, sd.maxHealth, waves ? waves->current : -1); fflush(stdout); }
			Fallen f;
			f.type = sd.type; f.face = sd.face; f.at = now; f.x = sd.x; f.y = sd.y; f.killed = sd.health <= 0;
			fallen.push_back(f);
			soldiers.erase(soldiers.begin() + i);
		}
		else i++;
	}
}

/* One soldier frame: its sheet is 64 px tiles, six across (three walking, three striking) and eight down (four ways of looking, then four of dying);
   a picture that is not a sheet (an older data folder) is drawn whole. */
static void soldier_frame(const SoldierType &st, int col, int row, float x, float y, int alpha)
{
	if (!st.picture.valid()) return;
	if (st.picture.w >= 384) draw_image_center(st.picture, x, y - 20, col * 64, row * 64, 64, 64, alpha, 0xFFFFFF);
	else draw_image_center(st.picture, x, y - 12, 0, 0, -1, -1, alpha, 0xFFFFFF);
}

void Map::drawSoldiers()
{
	int now = (int)gnow();
	const Image *boulder = NULL;
	for (size_t k = 0; k < trapTypes.size(); k++) if (trapTypes[k].kind == "boulder") boulder = &trapTypes[k].picture;
	for (size_t i = fallen.size(); i-- > 0;)   /* the fallen lie under the living */
	{
		const Fallen &f = fallen[i];
		const SoldierType &st = soldierTypes[f.type];
		int age = now - f.at;
		if (age < 0 || age > 1900) { fallen.erase(fallen.begin() + i); continue; }
		float x = f.x - camX, y = f.y - camY;
		if (f.killed)
		{
			int fr = age < 170 ? 0 : age < 340 ? 1 : 2;
			int alpha = age > 1400 ? 255 - (age - 1400) * 255 / 500 : 255;
			soldier_frame(st, fr, 4 + f.face, x, y, alpha);
		}
		else if (age < 500)   /* its time is up: it turns away and fades */
			soldier_frame(st, 1, f.face, x, y, 255 - age * 255 / 500);
	}
	for (size_t i = 0; i < soldiers.size(); i++)
	{
		const Soldier &sd = soldiers[i];
		const SoldierType &st = soldierTypes[sd.type];
		int col = 1;
		if (now - sd.strikeAt < 360) col = 3 + (now - sd.strikeAt) / 120;
		else if (sd.moving) { static const int cyc[4] = { 0, 1, 2, 1 }; col = cyc[(now / 140 + (int)i) & 3]; }
		float lat = (float)(((int)i % 3) - 1) * 9;   /* a squad stands in a loose row across the path, not all on one spot */
		float x = sd.x - camX - sd.dirY * lat, y = sd.y - camY + sd.dirX * lat;
		if (st.roller)
		{
			soldier_frame(st, col, sd.face, x + sd.dirX * 26, y + sd.dirY * 26, 255);   /* the pusher stands behind the boulder */
			if (boulder && boulder->valid()) draw_image_center(*boulder, x, y, 0, 0, -1, -1, 255, 0xFFFFFF);
		}
		else
		{
			if ((int)millis() < sd.flashUntil) draw_line((int)x, (int)(y - 14), (int)(sd.flashX - camX), (int)(sd.flashY - camY - 10), 0xFFFFFF);
			soldier_frame(st, col, sd.face, x, y, 255);
		}
		if (sd.health < sd.maxHealth)
		{
			int bw = 22, bx = (int)x - bw / 2, by = (int)y - 52;
			fill_rect(bx, by, bw, 3, 0xCC0033);
			fill_rect(bx, by, (int)(bw * (sd.health > 0 ? sd.health : 0) / sd.maxHealth), 3, 0x3399FF);
			rect_outline(bx - 1, by - 1, bw + 2, 5, 0x323232);
		}
	}
}

void Map::announceReady()
{
	static const char *nm[8] = { "VOLLEY", "RALLY", "MEND", "FROST HORN", "VERMIN HUNTERS", "GIANT SLAYERS", "SKYGUARD", "BOULDER ROLLERS" };
	for (int i = 0; i < 8; i++)
		if (!announced[i] && (int)gnow() >= specialReadyAt[i])
		{
			announced[i] = true;
			pushNotice(std::string(nm[i]) + " READY");
		}
}

void Map::applySupport()
{
	int now = (int)gnow();
	int dt = supportTick < 0 ? 0 : now - supportTick;
	supportTick = now;
	if (dt < 0 || dt > 1000) dt = 0;
	std::vector<Tower *> depots;
	for (size_t i = 0; i < towers.size(); i++)
		if (towers[i]->support && !towers[i]->destroyed) depots.push_back(towers[i]);
	for (size_t i = 0; i < towers.size(); i++)
	{
		Tower *t = towers[i];
		t->buffRate = 1.0f;
		if (t->support || t->destroyed) continue;
		if (depots.empty()) { t->buffRate = (int)gnow() < rallyUntil ? 1.0f + rallyPower / 100.0f : 1.0f; continue; }
		float tx, ty, rate = 0, heal = 0;
		t->center(tx, ty);
		for (size_t k = 0; k < depots.size(); k++)
		{
			float dx, dy;
			depots[k]->center(dx, dy);
			const SupportLevel &s = depots[k]->supportNow();
			const TowerMods &dm = towerMods[depots[k]->typeIdx];
			float range = s.range * dm.suprange;
			if ((tx - dx) * (tx - dx) + (ty - dy) * (ty - dy) <= range * range)
			{
				if (s.rate > rate) rate = s.rate;   /* the strongest depot counts, they do not add up */
				if (s.heal + dm.supheal > heal) heal = s.heal + dm.supheal;
			}
		}
		t->buffRate = (1.0f + rate / 100.0f) * ((int)gnow() < rallyUntil ? 1.0f + rallyPower / 100.0f : 1.0f);
		if (heal > 0 && t->health < t->maxHealth)
		{
			t->healCarry += t->maxHealth * heal / 100.0f * dt / 1000.0f;
			int h = (int)t->healCarry;
			if (h > 0)
			{
				t->health = t->health + h > t->maxHealth ? t->maxHealth : t->health + h;
				t->healCarry -= (float)h;
			}
		}
	}	/* soldiers standing in a depot's area mend too, lightly: a share (soldier_heal_share, half by default) of what it does for a tower */
	if (!depots.empty() && dt > 0)
	{
		float share = cfg_num("soldier_heal_share", 0.5f);
		for (size_t i = 0; i < soldiers.size(); i++)
		{
			Soldier &sd = soldiers[i];
			if (soldierTypes[sd.type].roller || sd.health <= 0 || sd.health >= sd.maxHealth) continue;
			float heal = 0;
			for (size_t k = 0; k < depots.size(); k++)
			{
				float dx, dy;
				depots[k]->center(dx, dy);
				const SupportLevel &s = depots[k]->supportNow();
				const TowerMods &dm = towerMods[depots[k]->typeIdx];
				float range = s.range * dm.suprange;
				if ((sd.x - dx) * (sd.x - dx) + (sd.y - dy) * (sd.y - dy) <= range * range && s.heal + dm.supheal > heal) heal = s.heal + dm.supheal;
			}
			if (heal <= 0) continue;
			sd.healCarry += sd.maxHealth * heal * share / 100.0f * dt / 1000.0f;
			if (sd.healCarry >= 1.0f)
			{
				float h = floorf(sd.healCarry);
				sd.health = sd.health + h > sd.maxHealth ? sd.maxHealth : sd.health + h;
				sd.healCarry -= h;
			}
		}
	}
}

float Map::supportGold(float x, float y, float killGold) const
{
	float best = 0;
	for (size_t i = 0; i < towers.size(); i++)
	{
		const Tower *d = towers[i];
		if (!d->support || d->destroyed) continue;
		float dx, dy;
		d->center(dx, dy);
		const SupportLevel &s = d->supportNow();
		const TowerMods &dm = towerMods[d->typeIdx];
		float range = s.range * dm.suprange;
		if ((x - dx) * (x - dx) + (y - dy) * (y - dy) <= range * range)
		{
			float g = s.gold + dm.supgold + killGold * s.goldPct / 100.0f;
			if (g > best) best = g;   /* the strongest depot counts */
		}
	}
	return best;
}

int Map::award(float gold)
{
	moneyCarry += gold;
	int g = (int)moneyCarry;
	moneyCarry -= (float)g;
	money += g;
	return g;
}

/* ------------------------------------------------------------------ wave */
Wave::Wave(int distance, const std::vector<const XNode *> &enemies, Map *map, const WaveScale &scale) : enemyDistance(distance)
{
	wholeDistance = gnow();
	for (size_t i = 0; i < enemies.size(); i++)
		for (int j = 0; j < (int)(enemies[i]->num("count") * scale.count + 0.5f); j++)
		{
			Enemy *e = new Enemy(*enemies[i], map, scale);
			if (j == 0)
				nextIcons.push_back(e->icon);
			list.push_back(e);
		}
}
Wave::~Wave() { for (size_t i = 0; i < list.size(); i++) delete list[i]; }

void Wave::start() { wholeDistance = gnow(); starting = true; sent = true; }

void Wave::calcEnemyDistance()
{
	if (!starting)
		return;
	/* the map is the owner of the way points */
	extern Map *g_map;
	size_t again = 0;
	for (size_t i = 0; i < list.size(); i++)
	{
		if ((int)gnow() - wholeDistance > (int)(i * enemyDistance) + offset)
		{
			if (!list[i]->wayPointsSet)
			{
				list[i]->setWaypoints(g_map->start, g_map->end, g_map->routeFor(list[i]->flying));
				again = i + 1;
			}
		}
		else
			break;
	}
	if (again == list.size())
		starting = false;
}

void Wave::displayGui()
{
	for (size_t i = 0; i < list.size(); i++)
		if ((int)gnow() - wholeDistance > (int)(i * enemyDistance) + offset)
			list[i]->displayGui();
}

void Wave::splash(Enemy *e, float damage, int radius, float factor)
{
	for (size_t i = 0; i < list.size(); i++)
	{
		Enemy *t = list[i];
		if (dist(e->position[0], e->position[1], t->position[0], t->position[1]) < radius && t != e && t->enemyAlive && !t->flying)   /* artillery splash does not reach flyers */
			t->hitByTower(damage * factor, 0);
	}
}

static bool further(const Enemy *a, const Enemy *b) { return a->moveComplete > b->moveComplete; }

Enemy *Wave::checkEnemy(int x, int y, int radius, float freeze, bool groundOnly)
{
	std::vector<Enemy *> sorted = list;
	std::stable_sort(sorted.begin(), sorted.end(), further);
	for (size_t i = 0; i < sorted.size(); i++)
	{
		Enemy *t = sorted[i];
		if (dist((float)(int)t->position[0], (float)(int)t->position[1], (float)x, (float)y) <= radius && t->alive() && t->position[0] > 0 && !(groundOnly && t->flying))
			if (freeze == 0 || !t->frozen || (t->freezeCount < (int)gnow() - t->freezeTime + 300))
				return t;
	}
	for (size_t i = 0; i < sorted.size(); i++)
	{
		Enemy *t = sorted[i];
		if (dist((float)(int)t->position[0], (float)(int)t->position[1], (float)x, (float)y) <= radius && t->alive() && t->position[0] > 0 && !(groundOnly && t->flying))
			return t;
	}
	return NULL;
}

bool Wave::complete() const
{
	for (size_t i = 0; i < list.size(); i++)
		if (list[i]->enemyAlive || !list[i]->enemyDied)
			return false;
	return true;
}

/* ------------------------------------------------------------------ waves */
Waves::Waves(const std::string &path, Map *m, bool isEndless) : map(m), endless(isEndless)
{
	if (!xml_load(path + "/waves.xml", root))
		return;
	std::vector<const XNode *> ws = root.list("waves/wave");
	for (size_t i = 0; i < ws.size(); i++)
	{
		std::vector<const XNode *> en;
		for (size_t k = 0; k < ws[i]->kids.size(); k++)
			if (ws[i]->kids[k].name == "enemy")
				en.push_back(&ws[i]->kids[k]);
		templates.push_back(en);
		spacing.push_back(ws[i]->num("enemydistance"));
		if (!endless)
		{
			WaveScale sc;
			sc.gold = gold_factor(m->difficulty, (int)i, (int)ws.size());
			waves.push_back(new Wave(spacing.back(), en, m, sc));
		}
	}
	if (endless && !templates.empty())
	{
		WaveScale sc0;
		sc0.gold = gold_factor(m->difficulty, 0, 1);
		waves.push_back(new Wave(spacing[0], templates[0], m, sc0));
		if (getenv("DM_ENDLESS_DUMP"))
			for (int k = 0; k < 40; k++) { current = k; ensureNext(); }
		current = 0;
	}
}
Waves::~Waves() { for (size_t i = 0; i < waves.size(); i++) delete waves[i]; }

/* Endless mode: wave w uses the map's wave w mod N as its pattern, and every creature gets tougher with each wave.
 * The rates are in defensemania.cfg; the gold per kill grows slower than the health, so a run always ends. */
void Waves::ensureNext()
{
	if (!endless || templates.empty() || current + 1 < (int)waves.size())
		return;
	int w = current + 1;
	WaveScale sc;
	sc.hp = powf(1.0f + (float)cfg_num("endless_hp_growth", 0.07), (float)w);
	sc.speed = fminf(1.4f, 1.0f + (float)cfg_num("endless_speed_growth", 0.008) * w);
	sc.count = fminf(2.5f, 1.0f + (float)cfg_num("endless_count_growth", 0.03) * w);
	sc.money = 1.0f + (float)cfg_num("endless_money_growth", 0.05) * w;
	sc.damage = 1.0f + (float)cfg_num("endless_damage_growth", 0.04) * w;
	sc.gold = gold_factor(map->difficulty, 0, 1);
	size_t t = (size_t)w % templates.size();
	int dist = (int)(spacing[t] * fmaxf(0.6f, 1.0f - 0.01f * w));
	waves.push_back(new Wave(dist, templates[t], map, sc));
	if (getenv("DM_ENDLESS_DUMP"))   /* test hook: what each endless wave is made of */
	{
		int hp = 0;
		for (size_t i = 0; i < waves.back()->list.size(); i++) hp += waves.back()->list[i]->maxHealthValue();
		printf("endless wave %d: pattern %d, hp x%.2f speed x%.2f count x%.2f money x%.2f -> %d enemies, %d hp in all\n",
		       w + 1, (int)t + 1, sc.hp, sc.speed, sc.count, sc.money, (int)waves.back()->list.size(), hp);
	}
}

void Waves::waveComplete()
{
	if (waves[current]->complete() && !map->defeated)
	{
		map->wavesCleared = current + 1;
		map->researchWaveDone();
		if (!endless && current + 1 >= (int)waves.size())
			map->wonGame();
		else
		{
			current++;
			ensureNext();
			map->continueNextWave();
			int every = (int)cfg_num("endless_bonus_every", 5);
			if (endless && every > 0 && current % every == 0)
			{   /* Endless: a supply drop every few waves, a little bigger each time, to keep players going */
				int milestone = current / every;
				float gold = (float)cfg_num("endless_bonus_gold", 120) * (1.0f + (float)cfg_num("endless_bonus_growth", 0.15) * (milestone - 1))
				             * (1.0f + (float)cfg_num("gold_difficulty_step", 0.10) * (map->difficulty - 1));
				int got = map->award(gold);
				map->stats.goldEarned += got;
				char b[64];
				snprintf(b, sizeof b, "SUPPLY DROP  +%d gold", got);
				map->pushNotice(b);
				sfx_play(SFX_UPGRADE);
			}
		}
	}
}

/* ------------------------------------------------------------------ tower */
Tower::Tower(const TowerType &t, int x, int y, Map *m, int type) : map(m)
{
	typeIdx = type;
	position[0] = x;
	position[1] = y;
	towerTileSize = t.tileSize;
	sheetFile = t.charset;
	sheet = map->charSets.add(sheetFile, t.tileSize);
	maxHealth = health = t.maxHealth;
	name = t.name;
	desc = t.description;
	splash = t.splash;
	splashRadius = t.splashRadius;
	splashFactor = t.splashFactor;
	fireRate = t.fireRate;
	fireRadius = t.fireRadius;
	damage = t.damage;
	costs = t.costs;
	freeze = t.freeze;
	updates = t.updates;
	explosionAtEnemy = t.explosionAtEnemy;
	hasShootingAnimation = t.hasShootingAnimation;
	damageIncrease = t.damageIncrease;
	support = t.support;
	groundOnly = t.groundOnly;
	for (int i = 0; i < 3; i++) { sup[i] = t.sup[i]; chill[i] = t.chill[i]; }
	for (int i = 0; i < 4; i++)
	{
		ucosts[i] = t.ucosts[i];
		udamage[i] = t.udamage[i];
		ufirerate[i] = t.ufirerate[i];
		ufireradius[i] = t.ufireradius[i];
	}
	enemyPosition[0] = enemyPosition[1] = hitAniPos[0] = hitAniPos[1] = arrowPosition[0] = arrowPosition[1] = 0;
}

std::string support_text(const SupportLevel &s, int tile)
{
	char b[200], g[64] = "";
	if (s.gold > 0 && s.goldPct > 0) snprintf(g, sizeof g, "+%.0f gold and +%.0f%% per kill", s.gold, s.goldPct);
	else if (s.gold > 0) snprintf(g, sizeof g, "+%.0f gold per kill", s.gold);
	else if (s.goldPct > 0) snprintf(g, sizeof g, "+%.0f%% gold per kill", s.goldPct);
	snprintf(b, sizeof b, "Within %.1f tiles: +%.0f%% fire rate, heals %.1f%%/s, %s", tile > 0 ? s.range / tile : 0.0f, s.rate, s.heal, g);
	return b;
}

std::string Tower::description() const
{
	if (support)
		return support_text(supportNow(), map->tileSize) + "\n" + desc;
	char b[160];
	snprintf(b, sizeof b, "DMG: %d   FR: %.1fs\n", damage, (float)fireRate / 1000);
	std::string extra;
	if (chillNow() > 0) { char c[64]; snprintf(c, sizeof c, "\nSlowed creatures take +%.0f%% damage", chillNow()); extra += c; }
	if (groundOnly) extra += "\nGround targets only";
	return std::string(b) + desc + extra;
}

int Tower::repairCosts() const
{
	return (int)((1.0f - ((float)health / (float)maxHealth)) * (10 + (currentUpdate * 10))) + 1;
}

void Tower::repair()
{
	if (map->money >= repairCosts() && health < maxHealth)
	{
		map->money -= repairCosts();
		map->stats.goldSpent += repairCosts();
		map->stats.repairs++;
		health = maxHealth;
		sfx_play(SFX_REPAIR);
	}
}

void Tower::upgrade()
{
	if (updates > 0 && updates != currentUpdate && map->money >= ucosts[currentUpdate])
	{
		damage = udamage[currentUpdate];
		fireRate = ufirerate[currentUpdate];
		fireRadius = ufireradius[currentUpdate];
		costs += ucosts[currentUpdate];
		map->money -= ucosts[currentUpdate];
		map->stats.goldSpent += ucosts[currentUpdate];
		map->stats.upgrades++;
		currentUpdate++;
		sfx_play(SFX_UPGRADE);
	}
}

void Tower::enemyDamage(float dmg, bool fromCreature)
{
	if (destroyed) return;
	if (fromCreature)
	{
		int now = (int)gnow();
		if (now - capStart >= 1000) { capStart = now; capTaken = 0; }
		float room = maxHealth * tower_damage_cap(map->difficulty) / 100.0f - capTaken;
		if (room <= 0) return;
		if (dmg > room) dmg = room;
		capTaken += dmg;
	}
	hitCarry += dmg;
	int whole = (int)hitCarry;
	hitCarry -= (float)whole;
	health -= whole;
	if (health < 0 && !destroyed)
	{
		destroyed = true;
		map->stats.towersLost++;
		destroyCount = gnow();
		sfx_play(SFX_CRUMBLE);
	}
}

void Tower::checkEnemy()
{
	if (!map->waves)
		return;
	int tile = map->tileSize;
	Enemy *e = map->waves->started ? map->waves->waves[map->waves->current]->checkEnemy((int)((position[0] + 0.5) * tile), (int)((position[1] + 0.5) * tile), (int)(((fireRadius / 2) + (towerTileSize / 4)) * map->towerMods[typeIdx].reach), freeze, groundOnly) : NULL;
	if (e)
	{
		shootAt = e;
		float fp[2];
		e->calcFuture((int)(fireRate * 0.5f), fp);
		shoot(fp[0], fp[1] - ((e->tileSize / 2) - (tile / 2)));
	}
}

void Tower::shoot(float x, float y)
{
	int tile = map->tileSize;
	shootDirection = x > ((position[0] + 0.5f) * tile) ? 1 : 0;
	float dx = ((position[0] + 0.5f) * tile) - x;
	shootAngle = atanf(((((position[1] + 0.5f) * tile) - ((towerTileSize - tile) / 2)) - y) / dx);
	enemyPosition[0] = x;
	enemyPosition[1] = y;
	shootingCount = gnow();
	shooting = true;
	map->stats.shots++;
	enemyLastHit = gnow();
	sfx_play(freeze > 0 ? SFX_FREEZE : splash ? SFX_CATAPULT : SFX_ARROW);
}

void Tower::center(float &x, float &y) const
{
	int tile = map->tileSize;
	x = (position[0] + 0.5f) * tile;
	y = ((position[1] + 0.5f) * tile) - ((towerTileSize - tile) / 2);
}
std::string Tower::debugState(bool withName) const
{
	char b[200];
	snprintf(b, sizeof b, "tower %d,%d (%s) upgrade %d, picture col %d row %d, animation %d dir %d, shooting %d step %d, drawingCount %d, destroyed %d", position[0], position[1], withName ? name.c_str() : "?", currentUpdate, dbgCol, dbgRow, animationState, towerDirection, (int)shooting, shootingAniState, drawingCount, (int)destroyed);
	return b;
}
static void hexdump_obj(const void *p, size_t n)
{
	const unsigned char *b = (const unsigned char *)p;
	for (size_t i = 0; i < n; i++)
		printf("%02x%s", b[i], (i % 16 == 15) ? "\n" : " ");
	printf("\n");
}
void Tower::display()
{
	if (position[0] < 0 || position[0] >= map->cols || position[1] < 0 || position[1] >= map->rows)
	{
		static int logged;   /* a tower that is nowhere on the map is memory that is not a tower any more: do not draw it */
		if (logged++ < 5)
		{
			printf("GHOST-HUNT: a tower object %p holds position %d,%d (map is %d x %d): %s; first 96 bytes:\n", (const void *)this, position[0], position[1], map->cols, map->rows, debugState(false).c_str());
			hexdump_obj(this, 96);
			fflush(stdout);
		}
		return;
	}
	g_curTower = this;
	struct Clear { ~Clear() { g_curTower = NULL; } } clearOnExit;
	int tile = map->tileSize;
	float cx = (position[0] + 0.5f) * tile, cy = ((position[1] + 0.5f) * tile) - ((towerTileSize - tile) / 2);
	if (!destroyed)
	{
		if (drawingCount >= 5)
		{
			float a = shootAngle + (shootDirection * PI_F);
			if ((int)gnow() - enemyLastHit > 3000) towerDirection = 2;
			else if (a > PI_F / 8 && a <= (PI_F * 3 / 8)) towerDirection = 1;
			else if (a > (PI_F * 3 / 8) && a <= (PI_F * 5 / 8)) towerDirection = 0;
			else if (a > (PI_F * 5 / 8) && a <= (PI_F * 7 / 8)) towerDirection = 5;
			else if (a > (PI_F * 7 / 8) && a <= (PI_F + (PI_F / 8))) towerDirection = 6;
			else if ((a > (PI_F + (PI_F / 8)) && a <= (PI_F + (PI_F * 3 / 8))) || (a < -(PI_F * 5 / 8) && a >= -(PI_F * 7 / 8))) towerDirection = 7;
			else if ((a > (PI_F + (PI_F * 3 / 8)) && a <= (PI_F + (PI_F * 5 / 8))) || (a < -(PI_F * 3 / 8) && a >= -(PI_F * 5 / 8))) towerDirection = 4;
			else if ((a > (PI_F + (PI_F * 5 / 8)) && a <= (PI_F + (PI_F * 7 / 8))) || (a < -(PI_F / 8) && a >= -(PI_F * 3 / 8))) towerDirection = 3;
			else towerDirection = 2;
			if (support) towerDirection = 0;   /* a depot has no front */
			uint32_t tint = 0xFFFFFF;
			if (selected)   /* the chosen tower pulses while its menu is open */
			{
				int b = 140 + (int)(115 * (0.5f + 0.5f * cosf(millis() / 160.0f)));
				tint = 0xFFFF00 | (b & 255);
			}
			if (shooting && shootingAniState < 3 && hasShootingAnimation)
			{
				dbgCol = (currentUpdate * 9) + shootingAniState + 3; dbgRow = towerDirection; if (frame_ok(sheet, dbgCol, dbgRow)) drawnFrame = g_mapFrame;
				frame_center(sheet, (currentUpdate * 9) + shootingAniState + 3, towerDirection, cx, cy, 255, tint);
				if ((int)gnow() - shootingCount > fireRate * 0.08f * (shootingAniState + 1) && shootingAniState < 3)
					shootingAniState++;
			}
			else
			{
				if ((float)((int)gnow() - animationCount) >= (1000 / 5.0f))
				{
					animationState += animationStateDirection;
					if (animationState == 3 || animationState == -1)
					{
						animationState = 1;
						animationStateDirection *= -1;
					}
					animationCount = gnow();
				}
				dbgCol = (currentUpdate * 9) + animationState; dbgRow = towerDirection; if (frame_ok(sheet, dbgCol, dbgRow)) drawnFrame = g_mapFrame;
				frame_center(sheet, (currentUpdate * 9) + animationState, towerDirection, cx, cy, 255, tint);
			}
			if (!support && (float)((int)gnow() - fireCount) >= (fireRate / (buffRate * map->towerMods[typeIdx].rate)))
			{
				checkEnemy();
				fireCount = gnow();
			}
		}
		else
			drawingCount++;
	}
	else
	{
		int dt = (int)gnow() - destroyCount, state = 8, alpha = 255;
		if (dt < 100) state = 6;
		else if (dt < 200) state = 7;
		else if (dt < 400) {}
		else if (dt < 7000)
		{
			alpha = destroyOpacity;
			destroyOpacity = destroyOpacity > 4 ? destroyOpacity - 10 : 0;
		}
		else
		{
			alpha = 0;
			map->removeTower(this);
		}
		frame_center(sheet, (currentUpdate * 9) + state, 0, cx, cy, alpha);
	}
}

void Tower::displayGui()
{
	int tile = map->tileSize;
	if (health < maxHealth && !destroyed)
	{
		float x = (position[0] * tile) + (towerTileSize * 0.1f) - camX, y = (position[1] * tile) - (towerTileSize * 0.25f) - camY;
		float w = tile * 0.7f;
		fill_rect((int)x, (int)y, (int)w, 3, 0xCC0033);
		fill_rect((int)x, (int)y, (int)(w * ((float)health / (float)maxHealth)), 3, 0x00CC33);
		rect_outline((int)x - 1, (int)y - 1, (int)w + 2, 5, 0x323232);
	}
	if (showRadius && !destroyed)
	{
		float cx = (position[0] + 0.5f) * tile - camX, cy = (position[1] + 0.5f) * tile - camY;
		float reach = support ? supportNow().range * map->towerMods[typeIdx].suprange : (fireRadius / 2.0f + towerTileSize / 4.0f) * map->towerMods[typeIdx].reach;   /* what the tower really reaches */
		uint32_t col = support ? 0x99CCFF : 0xCCFF99;
		fill_circle(cx, cy, reach, col, 60);
		circle_outline(cx, cy, reach, col, 200);
	}
}

void Tower::displayArrow()
{
	if (!shooting)
		return;
	int tile = map->tileSize;
	if ((int)gnow() - shootingCount < (fireRate * 0.5f))
	{
		float tx = (position[0] + 0.5f) * tile, ty = ((position[1] + 0.5f) * tile) - ((towerTileSize - tile) / 2);
		float c = dist(tx, ty, enemyPosition[0], enemyPosition[1]);
		float proz = (float)((int)gnow() - shootingCount) / (fireRate * 0.7f);
		float ang = shootAngle + (shootDirection * PI_F);
		arrowPosition[0] = cosf(ang) * (c - (proz * c));
		arrowPosition[1] = sinf(ang) * (c - (proz * c));
		int alpha = ((proz < 0.10f || proz > 0.95f)) ? 80 : 255;
		float ax = enemyPosition[0] + arrowPosition[0], ay = enemyPosition[1] + arrowPosition[1];
		if (!(ax - ax == 0) || !(ay - ay == 0) || dist(ax, ay, tx, ty) > c + tile)   /* never draw a shot that is not on its way */
		{
			static int logged;
			if (logged++ < 10)
			{
				printf("arrow glitch: tower %d,%d (%s) angle %.2f shot at %.0f,%.0f arrow at %.0f,%.0f after %d ms\n", position[0], position[1], name.c_str(), ang, enemyPosition[0], enemyPosition[1], ax, ay, (int)gnow() - shootingCount);
				fflush(stdout);
			}
			return;
		}
		if (proz > 0.05f && frame_ok(sheet, (currentUpdate * 9) + 6 + animationState, 2))
			draw_image_rot(sheet->img, ((currentUpdate * 9) + 6 + animationState) * sheet->tile, 2 * sheet->tile, sheet->tile, sheet->tile,
			               enemyPosition[0] + arrowPosition[0] - camX, enemyPosition[1] + arrowPosition[1] - camY, ang, alpha);
	}
	else
	{
		if (shootAt)
		{
			if (damageIncrease > 0)
				shootAt->hitByTower((float)damage * map->towerMods[typeIdx].damage, slowFactor(), this, damageIncrease + (damageIncrease > 0 ? map->towerMods[typeIdx].ramp : 0), chillNow() + map->towerMods[typeIdx].chill, slowMs());
			else
				shootAt->hitByTower((float)damage * map->towerMods[typeIdx].damage, slowFactor(), chillNow() + map->towerMods[typeIdx].chill, slowMs());
			if (splash && map->waves)
				map->waves->waves[map->waves->current]->splash(shootAt, (float)damage * map->towerMods[typeIdx].damage, (int)(splashRadius * map->towerMods[typeIdx].splash), splashFactor);
			if (explosionAtEnemy)
				shootAt->hitAnimation(sheetFile, 6 + currentUpdate * 9);
			else
			{
				hitAniCount = gnow();
				hitAniPos[0] = (float)(int)enemyPosition[0];
				hitAniPos[1] = (float)(int)enemyPosition[1];
				hitAniBool = true;
			}
		}
		shooting = false;
		shootingAniState = 0;
	}
}

void Tower::hitAnimation()
{
	if (!hitAniBool)
		return;
	int dt = (int)gnow() - hitAniCount;
	if (dt >= 240)
	{
		hitAniBool = false;
		return;
	}
	int step = dt < 80 ? 0 : dt < 160 ? 1 : 2;
	frame_center(sheet, 6 + currentUpdate * 9 + step, 1, hitAniPos[0], hitAniPos[1]);
}

/* ------------------------------------------------------------------ map */
Map *g_map = NULL;

Map::Map(const std::string &mapPath, int diff, bool isEndless) : path(mapPath), difficulty(diff), charSets(mapPath)
{
	gfx_reset_caches();
	endless = isEndless;
	g_map = this;
	size_t slash = mapPath.rfind('/');
	id = mapPath.substr(slash == std::string::npos ? 0 : slash + 1);
	if (id.size() > 4 && id.compare(id.size() - 4, 4, ".xml") == 0)
		id.resize(id.size() - 4);
	grade_set_map(id.c_str());
	globalSpeed = 1.0f;
	camX = camY = 0;
	game_clock_reset();

	XNode tree, wp, ts;
	if (!xml_load(path + "/maptree.xml", tree) || !xml_load(path + "/waypoints.xml", wp) || !xml_load(path + "/towers.xml", ts))
		return;
	tileSize = tree.child("tilesize")->num("s");
	cols = tree.child("mapsize")->num("w");
	rows = tree.child("mapsize")->num("h");
	mapW = cols * tileSize;
	mapH = rows * tileSize;
	money = tree.child("game")->num("startmoney");
	lifes = tree.child("game")->num("lifes");
	baseLifes = lifes;

	/* the collision layer: columns separated by '_', rows by '#', each tile "kind,variant" */
	std::vector<const XNode *> levels = tree.list("levels/level");
	if (levels.size() < 3)
		return;
	tileType.assign((size_t)cols * rows, 0);
	const std::string &t = levels[2]->text;
	int cx = 0, cy = 0;
	size_t i = 0;
	while (i < t.size() && cx < cols)
	{
		size_t e = i;
		while (e < t.size() && t[e] != ',')
			e++;
		int kind = atoi(t.substr(i, e - i).c_str());
		if (cy < rows)
			tileType[(size_t)cx * rows + cy] = kind;
		while (e < t.size() && t[e] != '#' && t[e] != '_')
			e++;
		if (e >= t.size())
			break;
		if (t[e] == '#') cy++;
		else { cx++; cy = 0; }
		i = e + 1;
	}

	std::vector<const XNode *> pts = wp.list("waypoints/waypoint");
	for (size_t k = 0; k < pts.size(); k++)
	{
		int x = atoi(pts[k]->text.c_str());
		size_t c = pts[k]->text.find(',');
		int y = c == std::string::npos ? 0 : atoi(pts[k]->text.c_str() + c + 1);
		if (k == 0) { start[0] = x; start[1] = y; }
		else if (k == pts.size() - 1) { end[0] = x; end[1] = y; }
		else way.push_back(std::make_pair(x, y));
	}
	/* <detour from="2" chance="50,30,10">x,y x,y ...</detour>: from is the id of the way point it leaves from */
	std::vector<const XNode *> dts = wp.list("waypoints/detour");
	for (size_t k = 0; k < dts.size(); k++)
	{
		Detour d;
		d.from = dts[k]->num("from");
		float c0 = 50, c1 = 50, c2 = 50;
		sscanf(dts[k]->str("chance", "50,50,50").c_str(), "%f,%f,%f", &c0, &c1, &c2);
		d.chance[0] = c0; d.chance[1] = c1; d.chance[2] = c2;
		const char *t = dts[k]->text.c_str();
		while (*t)
		{
			char *e;
			long x = strtol(t, &e, 10);
			if (e == t) { t++; continue; }
			if (*e != ',') break;
			long y = strtol(e + 1, &e, 10);
			d.out.push_back(std::make_pair((int)x, (int)y));
			t = e;
		}
		if (d.from >= 1 && d.from - 1 < (int)way.size() && !d.out.empty())
			detours.push_back(d);
	}

	std::vector<const XNode *> tw = ts.list("towers/tower");
	for (size_t k = 0; k < tw.size(); k++)
	{
		TowerType tt;
		const XNode &n = *tw[k];
		tt.name = n.str("name");
		tt.charset = n.str("charset");
		tt.icon = n.str("towericon");
		tt.description = n.text;
		while (!tt.description.empty() && isspace((unsigned char)tt.description[0])) tt.description.erase(0, 1);
		while (!tt.description.empty() && isspace((unsigned char)tt.description[tt.description.size() - 1])) tt.description.resize(tt.description.size() - 1);
		tt.tileSize = n.num("tilesize");
		tt.iconSize = n.num("towericonsize");
		tt.damage = n.num("damage");
		tt.fireRate = n.num("firerate");
		tt.fireRadius = n.num("fireradius");
		tt.splash = n.num("splash") != 0;
		tt.splashRadius = n.num("splashradius");
		tt.splashFactor = n.flt("splashfactor");
		tt.freeze = n.flt("freeze");
		tt.costs = n.num("costs");
		tt.maxHealth = n.num("maxhealth");
		tt.updates = n.num("updates");
		tt.explosionAtEnemy = n.num("explosionatenemy") == 1;
		tt.hasShootingAnimation = n.num("hasshootinganimation") == 1;
		tt.damageIncrease = n.flt("damageincrease");
		tt.support = n.num("support") != 0;
		tt.groundOnly = n.num("groundonly") != 0;
		for (int lvl = 0; lvl < 3; lvl++)
		{
			auto g = [&](const char *base, float prev) -> float {
				char key[48];
				if (lvl == 0) snprintf(key, sizeof key, "%s", base); else snprintf(key, sizeof key, "u%d-%s", lvl, base);
				return n.str(key, "").empty() ? prev : n.flt(key);
			};
			const SupportLevel &pv = lvl ? tt.sup[lvl - 1] : tt.sup[0];
			tt.sup[lvl].range = g("aurarange", lvl ? pv.range : 0);
			tt.sup[lvl].rate = g("supportrate", lvl ? pv.rate : 0);
			tt.sup[lvl].heal = g("supportheal", lvl ? pv.heal : 0);
			tt.sup[lvl].gold = g("supportgold", lvl ? pv.gold : 0);
			tt.sup[lvl].goldPct = g("supportgoldpct", lvl ? pv.goldPct : 0);
			tt.chill[lvl] = g("chill", lvl ? tt.chill[lvl - 1] : 0);
		}
		for (int j = 0; j < 4; j++)
		{
			char key[24];
			snprintf(key, sizeof key, "u%d-costs", j + 1);     tt.ucosts[j] = n.num(key);
			snprintf(key, sizeof key, "u%d-damage", j + 1);    tt.udamage[j] = n.num(key);
			snprintf(key, sizeof key, "u%d-firerate", j + 1);  tt.ufirerate[j] = n.num(key);
			snprintf(key, sizeof key, "u%d-fireradius", j + 1); tt.ufireradius[j] = n.num(key);
		}
		load_image(path + "/data/" + tt.icon, tt.iconImg);
		charSets.add(tt.charset, tt.tileSize);   /* loaded now, so building a tower never stalls the game */
		towerTypes.push_back(tt);
	}

	bastionX = (float)end[0]; bastionY = (float)end[1];
	pathPts.push_back(std::make_pair((float)start[0], (float)start[1]));
	for (size_t i = 0; i < way.size(); i++) pathPts.push_back(std::make_pair((float)way[i].first, (float)way[i].second));
	pathPts.push_back(std::make_pair((float)end[0], (float)end[1]));
	placeKeep();
	{
		XNode tr;
		if (xml_load("data/traps.xml", tr))
		{
			std::vector<const XNode *> tl = tr.list("trap");
			for (size_t k = 0; k < tl.size(); k++)
			{
				TrapType t;
				t.id = tl[k]->str("id"); t.name = tl[k]->str("name"); t.kind = tl[k]->str("kind");
				t.text = tl[k]->text;
				while (!t.text.empty() && isspace((unsigned char)t.text[0])) t.text.erase(0, 1);
				while (!t.text.empty() && isspace((unsigned char)t.text[t.text.size() - 1])) t.text.resize(t.text.size() - 1);
				t.costs = tl[k]->num("costs"); t.cap = tl[k]->num("cap"); t.charges = tl[k]->num("charges"); t.interval = tl[k]->num("interval");
				t.duration = tl[k]->num("duration"); t.health = tl[k]->num("health"); t.attackers = tl[k]->num("attackers");
				t.radius = tl[k]->flt("radius"); t.damage = tl[k]->flt("damage"); t.slow = tl[k]->flt("slow", 1);
				load_image("data/traps/" + tl[k]->str("icon"), t.icon);
				load_image("data/traps/" + tl[k]->str("picture"), t.picture);
				if (!tl[k]->str("crack1").empty()) load_image("data/traps/" + tl[k]->str("crack1"), t.crack[0]);
				if (!tl[k]->str("crack2").empty()) load_image("data/traps/" + tl[k]->str("crack2"), t.crack[1]);
				trapTypes.push_back(t);
			}
		}
	}
	{
		XNode rs;
		if (xml_load("data/research.xml", rs))
		{
			std::vector<const XNode *> ll = rs.list("line");
			for (size_t k = 0; k < ll.size(); k++)
			{
				ResearchLine r;
				r.id = ll[k]->str("id"); r.group = ll[k]->str("group"); r.target = ll[k]->str("target"); r.title = ll[k]->str("title");
				std::vector<const XNode *> tl = ll[k]->list("tier");
				for (size_t j = 0; j < tl.size(); j++)
				{
					ResearchTier t;
					t.name = tl[j]->str("name"); t.cost = tl[j]->num("cost"); t.waves = tl[j]->num("waves");
					t.text = tl[j]->text;
					while (!t.text.empty() && isspace((unsigned char)t.text[0])) t.text.erase(0, 1);
					while (!t.text.empty() && isspace((unsigned char)t.text[t.text.size() - 1])) t.text.resize(t.text.size() - 1);
					for (size_t a = 0; a < tl[j]->attrs.size(); a++)
					{
						const std::string &key = tl[j]->attrs[a].first;
						if (key != "name" && key != "cost" && key != "waves") t.eff.push_back(std::make_pair(key, (float)atof(tl[j]->attrs[a].second.c_str())));
					}
					r.tier.push_back(t);
				}
				research.push_back(r);
			}
		}
	}
	{
		XNode so;
		if (xml_load("data/soldiers.xml", so))
		{
			soldierOff = so.flt("off", 0.3f);
			std::vector<const XNode *> sl = so.list("soldier");
			for (size_t k = 0; k < sl.size(); k++)
			{
				SoldierType t;
				t.id = sl[k]->str("id"); t.name = sl[k]->str("name"); t.text = sl[k]->text;
				while (!t.text.empty() && isspace((unsigned char)t.text[0])) t.text.erase(0, 1);
				t.health = sl[k]->flt("health"); t.damage = sl[k]->flt("damage"); t.pct = sl[k]->flt("pct"); t.rate = sl[k]->flt("rate", 1000); t.reach = sl[k]->flt("reach", 28); t.speed = sl[k]->flt("speed", 40);
				t.wear = sl[k]->flt("wear"); t.wearpct = sl[k]->flt("wearpct");
				t.pierce = sl[k]->num("pierce") != 0; t.ranged = sl[k]->num("ranged") != 0; t.roller = sl[k]->num("roller") != 0;
				std::string vs = sl[k]->str("vs");
				for (size_t a = 0; a < vs.size();)
				{
					size_t b = vs.find(' ', a);
					if (b == std::string::npos) b = vs.size();
					if (b > a) t.vs.push_back(vs.substr(a, b - a));
					a = b + 1;
				}
				load_image("data/soldiers/" + sl[k]->str("picture"), t.picture);
				soldierTypes.push_back(t);
			}
		}
	}
	towerMods.assign(towerTypes.size(), TowerMods());
	trapMods.assign(trapTypes.size(), TrapMods());
	if (!load_image565(path + "/data/map.png", mapImage))
		return;
	load_image("data/hud_top.png", hudTop);
	{   /* a map may have its own plank (data/hud/<map>.png, 160 wide); a map without one uses the plain plank */
		std::string hp = "data/hud/" + id + ".png";
		FILE *hf = fopen(hp.c_str(), "rb");
		if (hf) { fclose(hf); load_image(hp, hudTopMap); }
	}
	load_image("data/hud_bottom.png", hudBottom);
	load_image("data/money.png", moneyImg);
	load_image("data/bastion.png", bastionImg);
	load_image("data/life.png", lifeImg);
	load_image("data/hud_pause.png", hudPause);
	load_image("data/hud_play.png", hudPlay);
	load_image("data/build.png", hudBuild);
	load_image("data/menu.png", hudMenu);
	if (optHQ && load_image("data/ray1.png", lightRay))
	{
		/* The maps stand still, so the light rays are blended into the map picture once, here, instead of over every
		 * frame (that cost about 60 ms a frame on the device). */
		uint32_t t0 = millis();
		bake_overlay565(mapImage, lightRay);
		lightRay = Image();
		printf("light rays baked into the map in %u ms\n", (unsigned)(millis() - t0));
		fflush(stdout);
	}
	waves = new Waves(path, this, endless);
	ok = !waves->waves.empty();
	startMessage();
}

Map::~Map()
{
	gfx_reset_caches();
	for (size_t i = 0; i < towers.size(); i++) delete towers[i];
	delete waves;
	if (g_map == this)
		g_map = NULL;
}

bool Map::canBuild(int x, int y) const
{
	int tx = x / tileSize, ty = y / tileSize;
	if (x < 0 || y < 0 || tx >= cols || ty >= rows)
		return false;
	if (keepBlocks(tx * tileSize + tileSize / 2, ty * tileSize + tileSize / 2)) return false;
	int k = tileType[(size_t)tx * rows + ty];
	return !(k == 1 || k == 3 || k == 4 || k == 5);
}

int Map::towerAt(int x, int y) const
{
	int tx = x / tileSize, ty = y / tileSize;
	for (size_t i = 0; i < towers.size(); i++)
		if (towers[i]->position[0] == tx && towers[i]->position[1] == ty && !towers[i]->destroyed)
			return (int)i;
	return -1;
}

void Map::addTower(int type, int tx, int ty)
{
	const TowerType &t = towerTypes[type];
	if (t.costs <= money)
	{
		if (!gameStarted)
			startGame();
		money -= t.costs;
		stats.goldSpent += t.costs;
		if (type < 8) stats.built[type]++;
		towers.push_back(new Tower(t, tx, ty, this, type));
		sfx_play(SFX_BUILD);
	}
	else
		sfx_play(SFX_ERROR);
}

void Map::sellTower(Tower *t)
{
	money += (int)(t->costs * 0.8);
	stats.goldSold += (int)(t->costs * 0.8);
	stats.sold++;
	removeTower(t);
	sfx_play(SFX_SELL);
}

static std::vector<Tower *> g_graveyard;
void Map::removeTower(Tower *t)
{
	for (size_t i = 0; i < towers.size(); i++)
		if (towers[i] == t)
		{
			towers.erase(towers.begin() + i);
			g_graveyard.push_back(t);   /* freed after the frame, the tower may still be drawing */
			break;
		}
}

void Map::checkTowers(int x, int y, int radius, float damage)
{
	for (size_t i = 0; i < towers.size(); i++)
		if (dist((towers[i]->position[0] + 0.5f) * tileSize, (towers[i]->position[1] + 0.5f) * tileSize, (float)x, (float)y) <= radius)
			towers[i]->enemyDamage(damage, true);
	soldierDamage((float)x, (float)y, (float)radius, damage);
}

struct DrawItem { float y; Tower *tower; Enemy *enemy; };
static bool by_y(const DrawItem &a, const DrawItem &b) { return a.y < b.y; }

int g_prof[5], g_profEnemies, g_profTowers;   /* ms spent in the parts of the last Map::display(), for the hitch log */

void Map::display()
{
	g_mapFrame++;
	g_drn = 0;
	uint32_t p0 = millis();
	game_clock_advance();
	applySupport();
	updateTraps();
	updateSoldiers();
	announceReady();
	updateNotices();
	if (showBuildAreas)
	{
		if (areasImage.px.empty())   /* once: a copy of the map with the build areas blended in, so showing them costs nothing per frame */
		{
			uint32_t t0 = millis();
			areasImage = mapImage;
			for (int i = 0; i < cols; i++)
				for (int j = 0; j < rows; j++)
				{
					int k = tileType[(size_t)i * rows + j];
					if (k != 1 && k != 3 && k != 4 && k != 5)
						tint565_rect(areasImage, i * tileSize + 1, j * tileSize + 1, tileSize - 2, tileSize - 2, 0x339933, 110);
				}
			printf("build areas baked into a copy of the map in %u ms\n", (unsigned)(millis() - t0));
			fflush(stdout);
		}
		blit565_window(areasImage, camX, camY);
	}
	else
		blit565_window(mapImage, camX, camY);
	drawTraps();
	if (bastionImg.valid()) draw_image_center(bastionImg, keepX - camX, keepY - camY - bastionImg.h / 2 + 6);
	if ((int)millis() < volleyUntil)
	{
		float f = (volleyUntil - (int)millis()) / 700.0f;
		circle_outline(bastionX - camX, bastionY - camY, 220, 0xFFD070, (int)(220 * f));
	}
	uint32_t p1 = millis();
	/* towers and enemies, back to front */
	std::vector<DrawItem> items;
	g_profEnemies = 0;
	for (size_t i = 0; i < towers.size(); i++)
	{
		DrawItem d = { (towers[i]->position[1] + 0.5f) * tileSize, towers[i], NULL };
		items.push_back(d);
	}
	if (waves && waves->started)
	{
		Wave *w = waves->waves[waves->current];
		for (size_t i = 0; i < w->list.size(); i++)
		{
			Enemy *e = w->list[i];
			float y = e->position[1];
			if (!e->enemyDied && !e->enemyAlive && (e->moveDirection == 1 || e->moveDirection == 2))
				y = -50;
			DrawItem d = { y, NULL, e };
			items.push_back(d);
			if (e->enemyAlive) g_profEnemies++;
		}
		waves->waveComplete();
		w->calcEnemyDistance();
	}
	g_profTowers = (int)towers.size();
	uint32_t p2 = millis();
	std::stable_sort(items.begin(), items.end(), by_y);
	for (size_t i = 0; i < items.size(); i++)
	{
		if (items[i].tower) items[i].tower->display();
		else items[i].enemy->display();
	}
	drawSoldiers();
	uint32_t p3 = millis();
	if (waves)
		waves->displayGui();
	for (size_t i = 0; i < towers.size(); i++)
		towers[i]->displayGui();
	for (size_t i = 0; i < towers.size(); i++)
	{
		towers[i]->displayArrow();
		towers[i]->hitAnimation();
	}
	if (lightRay.valid())
		draw_image(lightRay, -camX, -camY);
	for (size_t i = 0; i < g_graveyard.size(); i++)
		delete g_graveyard[i];
	g_graveyard.clear();
	{
		/* the ghost hunt: a tower drawn last frame but not this one, or a tower picture drawn where no tower is */
		static int logged = 0;
		if (logged < 60)
		{
			for (size_t i = 0; i < towers.size(); i++)
				if (!towers[i]->destroyed && towers[i]->drawnFrame == g_mapFrame - 1)
				{
					logged++;
					printf("GHOST-HUNT frame %d: a tower was not drawn this frame: %s; camera %d,%d, %d sprites drawn\n", g_mapFrame, towers[i]->debugState().c_str(), camX, camY, g_drn);
					fflush(stdout);
				}
			for (int d = 0; d < g_drn; d++)
			{
				bool isTowerSheet = false, matched = false;
				for (size_t i = 0; i < towers.size(); i++)
					if (towers[i]->sheetPtr() == g_dr[d].s)
					{
						isTowerSheet = true;
						float x, y;
						towers[i]->center(x, y);
						if (fabsf(x - g_dr[d].cx) < 0.6f && fabsf(y - g_dr[d].cy) < 0.6f)
							matched = true;
					}
				if (isTowerSheet && !matched && (g_dr[d].col % 9) <= 5 && g_dr[d].row < 8)
				{
					logged++;
					bool inList = false;
					for (size_t i = 0; i < towers.size(); i++)
						if (towers[i] == g_dr[d].who) inList = true;
					printf("GHOST-HUNT frame %d: a tower picture (col %d row %d) was drawn at %.1f,%.1f where no tower is; camera %d,%d, %d sprites drawn; drawn by object %p (%s in the tower list, %d towers)\n", g_mapFrame, g_dr[d].col, g_dr[d].row, g_dr[d].cx, g_dr[d].cy, camX, camY, g_drn, (const void *)g_dr[d].who, inList ? "is" : "is NOT", (int)towers.size());
					if (g_dr[d].who)
					{
						printf("  %s\n", g_dr[d].who->debugState(false).c_str());
						hexdump_obj(g_dr[d].who, 96);
					}
					fflush(stdout);
				}
			}
		}
	}
	uint32_t p4 = millis();
	g_prof[0] = (int)(p1 - p0);   /* map picture and build areas */
	g_prof[1] = (int)(p2 - p1);   /* making the list of what to draw */
	g_prof[2] = (int)(p3 - p2);   /* towers and enemies */
	g_prof[3] = (int)(p4 - p3);   /* bars, shots, hit animations */
}

/* ------------------------------------------------------------------ flow and the on-screen messages */
void Map::startMessage() { actionTimer = millis(); action = 5; }
void Map::startGame() { gameStarted = true; actionTimer = millis(); action = 1; }
void Map::continueNextWave() { actionTimer = millis(); action = 2; }
void Map::wonGame() { actionTimer = millis(); action = 3; globalSpeed = 0; defeated = true; sfx_play(SFX_WIN); }
void Map::loseGame() { actionTimer = millis(); action = 4; defeated = true; globalSpeed = 0; sfx_play(SFX_LOSE); }

static void big_text(const char *s, int size, int yoff = 0)
{
	draw_text(s, SW / 2, SH / 2 + yoff, size, 0xFFFFFF, ALIGN_CENTER, 2, 255, 0x000000);
}

void Map::displayActions()
{
	int dt = (int)millis() - actionTimer;
	switch (action)
	{
	case 1:
		if (dt <= 8000)
		{
			static int lastStep;
			int step = dt <= 3000 ? 0 : dt <= 4000 ? 1 : dt <= 5000 ? 2 : dt <= 6000 ? 3 : 4;
			if (step != lastStep && step > 0)
				sfx_play(step == 4 ? SFX_GO : SFX_TICK);
			lastStep = step;
			if (step == 0) big_text("Prepare!", 34, -20);
			else if (step == 1) big_text("3", 52, -30);
			else if (step == 2) big_text("2", 52, -30);
			else if (step == 3) big_text("1", 52, -30);
			else big_text("GO!", 56, -34);
		}
		else
		{
			waves->nextWave();
			sfx_play(SFX_WAVE);
			action = 0;
		}
		break;
	case 2:
		if (dt >= 2000)
		{
			waves->nextWave();
			sfx_play(SFX_WAVE);
			action = 0;
		}
		break;
	case 3:
		if (dt <= 3000) big_text("Victory!", 48, -30);
		else { action = 0; finished = true; finishedWon = true; }
		break;
	case 4:
		if (dt <= 3000) big_text("Defeated!", 48, -30);
		else { action = 0; finished = true; finishedWon = false; }
		break;
	case 5:
		if (dt <= 5000)
		{
			draw_text("The game will start when\nyou build the first tower", SW / 2, SH / 2 - 24, 17, 0xFFFFFF, ALIGN_CENTER, 2, 255, 0x000000);
		}
		break;
	}
}

void Map::drawHud()
{
	/* money and lives, top left: room for seven digits of gold, so the plank is stretched to fit and the heart sits after them */
	char b[32];
	const int goldX = 26, goldW = text_width("9999999", 13, 2);
	const int heartX = goldX + goldW + 10, livesX = heartX + 18, plankW = livesX + text_width("999", 13, 2) + 16;
	if (hudTopMap.valid())
	{   /* the map's own plank, 160 wide: its end stays at the right edge; a different width repeats the plain strip at columns 70-86 or crops */
		const int endW = 16, stripX = 70, stripW = 16, w = hudTopMap.w;
		int bodyW = plankW - endW;
		if (bodyW > w - endW)
		{
			draw_image(hudTopMap, 0, 0, 0, 0, w - endW, hudTopMap.h);
			for (int x = w - endW; x < bodyW; x += stripW)
				draw_image(hudTopMap, x, 0, stripX, 0, bodyW - x < stripW ? bodyW - x : stripW, hudTopMap.h);
		}
		else
			draw_image(hudTopMap, 0, 0, 0, 0, bodyW, hudTopMap.h);
		draw_image(hudTopMap, bodyW, 0, w - endW, 0, endW, hudTopMap.h);
	}
	else if (plankW <= hudTop.w)
		draw_image(hudTop, 0, 0);
	else
	{   /* the plank's left part, its middle repeated as often as needed, then its ragged right end */
		const int endW = 16, midX = 50, midW = 50;
		int bodyW = plankW - endW, x = 0;
		draw_image(hudTop, 0, 0, 0, 0, midX, hudTop.h);
		x = midX;
		while (x < bodyW)
		{
			int w = bodyW - x < midW ? bodyW - x : midW;
			draw_image(hudTop, x, 0, midX, 0, w, hudTop.h);
			x += w;
		}
		draw_image(hudTop, bodyW, 0, hudTop.w - endW, 0, endW, hudTop.h);
	}
	draw_image(moneyImg, 8, 13);   /* the gold nugget, level with the heart */
	snprintf(b, sizeof b, "%d", money);
	draw_text(b, goldX, 8, 13, TEXT_GOLD, ALIGN_LEFT, 2, 255, 0x000000);
	snprintf(b, sizeof b, "SCORE %d", runningScore());
	draw_text(b, 8, 40, 11, 0xFFFFFF, ALIGN_LEFT, 2, 255, 0x000000);
	draw_image(lifeImg, heartX, 11);
	snprintf(b, sizeof b, "%d", lifes);
	draw_text(b, livesX, 8, 13, TEXT_GOLD, ALIGN_LEFT, 2, 255, 0x000000);
	/* wave information, bottom right */
	draw_image(hudBottom, SW - 400, SH - 50);
	if (endless)
		snprintf(b, sizeof b, "Wave %d - next wave:", waves->current + 1);
	else
		snprintf(b, sizeof b, "Wave %d of %d - next wave:", waves->current + 1, (int)waves->waves.size());
	draw_text(b, SW - 358, SH - 30, 11, TEXT_GOLD, ALIGN_LEFT, 2, 255, 0x000000);
	if (waves->current + 1 < (int)waves->waves.size() && waves->waves[waves->current + 1])
	{
		Wave *nw = waves->waves[waves->current + 1];
		int iconX = SW - 358 + text_width(b, 11, 2) + 8;   /* right after the words, as in the original */
		for (size_t i = 0; i < nw->nextIcons.size(); i++)
			if (nw->nextIcons[i])
				draw_image(nw->nextIcons[i]->img, iconX + (int)i * 32, SH - 30);
	}
	/* the three buttons, bottom left, each with the pad button that works it */
	draw_image(pause ? hudPlay : hudPause, 13, SH - 49);
	draw_image(hudBuild, 65, SH - 47);
	if (bastionImg.valid()) draw_image_scaled(bastionImg, 110, SH - 50, 18, 36);   /* the keep: Start opens its menu */
	else draw_image(hudMenu, 105, SH - 47);
	draw_text("SELECT", 31, SH - 13, 10, 0xFFFFFF, ALIGN_CENTER, 2, 255, 0x000000);   /* the pause menu */
	draw_text("X", 76, SH - 13, 10, 0xFFFFFF, ALIGN_CENTER, 2, 255, 0x000000);
	draw_text("START", 119, SH - 13, 10, 0xFFFFFF, ALIGN_CENTER, 2, 255, 0x000000);
}
