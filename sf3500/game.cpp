#include "game.hpp"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float globalSpeed = 1.0f;
bool optShowFps = false, optHQ = false;
int camX = 0, camY = 0;

static const float PI_F = 3.14159265f;
static const uint32_t TEXT_GOLD = 0xFFF8C8;

static float dist(float x1, float y1, float x2, float y2) { float dx = x1 - x2, dy = y1 - y2; return sqrtf(dx * dx + dy * dy); }

/* one 64x64 (or whatever the sheet's tile is) frame, centred or by its corner, in world coordinates */
static bool frame_ok(const Sheet *s, int col, int row)
{
	return s && s->img.valid() && col >= 0 && row >= 0 && (s->tile <= 0 || ((col + 1) * s->tile <= s->img.w && (row + 1) * s->tile <= s->img.h));
}
static void frame_center(const Sheet *s, int col, int row, float cx, float cy, int alpha = 255, uint32_t tint = 0xFFFFFF)
{
	if (!frame_ok(s, col, row))
		return;
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
Enemy::Enemy(const XNode &def, Map *m) : map(m)
{
	position[0] = position[1] = -16;
	moveVector[0] = moveVector[1] = 0;
	sheetFile = def.str("charset");
	tileSize = def.num("tilesize");
	sheet = map->charSets.add(sheetFile, tileSize);
	icon = map->charSets.add(def.str("icon"), -1);
	maxHealth = health = def.num("maxhealth") * map->difficulty;
	moveSpeed = def.num("movespeed");
	money = def.num("money");
	damage = def.num("damage") * map->difficulty;
	animationSpeed = (float)def.num("animationspeed");
	moveCount = millis();
}

void Enemy::calcFuture(int timeMs, float out[2]) const
{
	float px = ((float)moveSpeed / 1000) * timeMs;
	out[0] = position[0] + moveVector[0] * globalSpeed * freezeFactorTemp * px;
	out[1] = position[1] + moveVector[1] * globalSpeed * freezeFactorTemp * px;
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
	moveCount = millis();
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
	moveCount = millis();
}

void Enemy::calculateMoveVector()
{
	wpPosition++;
	moveVector[0] = moveVector[1] = 0;
	float nx, ny;
	if (wpPosition >= (int)way.size()) { nx = (float)end[0]; ny = (float)end[1]; }
	else { nx = (float)way[wpPosition].first; ny = (float)way[wpPosition].second; }
	if (nx - position[0] > 0 + freezeFactorTemp) { moveVector[0] = 1; moveDirection = 2; }
	else if (nx - position[0] < 0 - freezeFactorTemp) { moveVector[0] = -1; moveDirection = 1; }
	if (ny - position[1] > 0 + freezeFactorTemp) { moveVector[1] = 1; moveDirection = 0; }
	else if (ny - position[1] < 0 - freezeFactorTemp) { moveVector[1] = -1; moveDirection = 3; }
}

void Enemy::checkWP()
{
	int nx, ny;
	if (wpPosition >= (int)way.size())
	{
		nx = end[0] + tileSize;
		ny = end[1];
		reachingEndpoint = true;
	}
	else
	{
		nx = way[wpPosition].first;
		ny = way[wpPosition].second;
	}
	bool arrived = false;
	if (moveVector[0] < 0) { if (position[0] <= nx) { position[0] = (float)nx; arrived = true; } }
	else if (moveVector[0] > 0) { if (position[0] >= nx) { position[0] = (float)nx; arrived = true; } }
	else if (moveVector[1] < 0) { if (position[1] <= ny) { position[1] = (float)ny; arrived = true; } }
	else if (moveVector[1] > 0) { if (position[1] >= ny) { position[1] = (float)ny; arrived = true; } }
	if (arrived)
	{
		if (reachingEndpoint)
		{
			map->lifes--;
			if (map->lifes == 0)
				map->loseGame();
			enemyAlive = false;
		}
		calculateMoveVector();
	}
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
			if ((int)millis() - dieCount > 100)
			{
				dieCount = millis();
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
		if (freezeCount > (int)millis() - freezeTime)
		{
			freezeFactorTemp = freezeFactor;
			tint = 0x0099CC;
		}
		else
			frozen = false;
		if (damage > 0 && (float)((int)millis() - fireCount) >= (fireRate / (globalSpeed * freezeFactorTemp)))
		{
			map->checkTowers((int)position[0], (int)position[1], fireRadius, damage);
			fireCount = millis();
		}
		if ((float)((int)millis() - animationCount) >= (1000 / (animationSpeed * globalSpeed * freezeFactorTemp)))
		{
			moveState += moveStateDirection;
			if (moveState == 3 || moveState == -1)
			{
				moveState = 1;
				moveStateDirection *= -1;
			}
			animationCount = millis();
		}
		checkWP();
		float dt = (float)((int)millis() - moveCount);
		float px = ((float)moveSpeed / 1000) * dt;
		position[0] += moveVector[0] * globalSpeed * freezeFactorTemp * px;
		position[1] += moveVector[1] * globalSpeed * freezeFactorTemp * px;
		moveComplete += fabsf(moveVector[0] * globalSpeed * freezeFactorTemp * px) + fabsf(moveVector[1] * globalSpeed * freezeFactorTemp * px);
		moveCount = millis();
		frame_corner(sheet, moveState, moveDirection, drawX, drawY, 255, tint);
	}
	if (gotHit)
	{
		Sheet *hs = map->charSets.get(hitCharset);
		int dt = (int)millis() - hitCount, step = dt < 80 ? 0 : dt < 160 ? 1 : dt < 240 ? 2 : -1;
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

void Enemy::getDamage(int dmg, float freeze)
{
	if (freeze > 0)
	{
		freezeCount = millis();
		freezeFactor = freeze;
		frozen = true;
		freezeTime = 2500;
	}
	health -= dmg;
	if (health < 0)
	{
		moveComplete = 999999999;
		enemyAlive = false;
		dieCount = millis();
		map->money += money;
	}
}

void Enemy::hitByTower(int dmg, float freeze) { getDamage(dmg, freeze); }

void Enemy::hitByTower(int dmg, float freeze, Tower *tower, float multiplier)
{
	int hitDamage = dmg;
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
		TowerHit h = { tower, 1, dmg, multiplier };
		towerHits.push_back(h);
	}
	getDamage(hitDamage, freeze);
}

void Enemy::hitAnimation(const std::string &cs, int x)
{
	gotHit = true;
	hitCount = millis();
	hitX = x;
	hitCharset = cs;
}

/* ------------------------------------------------------------------ wave */
Wave::Wave(int distance, const std::vector<const XNode *> &enemies, Map *map) : enemyDistance(distance)
{
	wholeDistance = millis();
	for (size_t i = 0; i < enemies.size(); i++)
		for (int j = 0; j < enemies[i]->num("count"); j++)
		{
			Enemy *e = new Enemy(*enemies[i], map);
			if (j == 0)
				nextIcons.push_back(e->icon);
			list.push_back(e);
		}
}
Wave::~Wave() { for (size_t i = 0; i < list.size(); i++) delete list[i]; }

void Wave::start() { wholeDistance = millis(); starting = true; }

void Wave::calcEnemyDistance()
{
	if (!starting)
		return;
	/* the map is the owner of the way points */
	extern Map *g_map;
	size_t again = 0;
	for (size_t i = 0; i < list.size(); i++)
	{
		if ((int)millis() - wholeDistance > (int)(i * enemyDistance) + offset)
		{
			if (!list[i]->wayPointsSet)
			{
				list[i]->setWaypoints(g_map->start, g_map->end, g_map->way);
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
		if ((int)millis() - wholeDistance > (int)(i * enemyDistance) + offset)
			list[i]->displayGui();
}

void Wave::splash(Enemy *e, int damage, int radius, float factor)
{
	for (size_t i = 0; i < list.size(); i++)
	{
		Enemy *t = list[i];
		if (dist(e->position[0], e->position[1], t->position[0], t->position[1]) < radius && t != e && t->enemyAlive)
			t->hitByTower((int)(damage * factor), 0);
	}
}

static bool further(const Enemy *a, const Enemy *b) { return a->moveComplete > b->moveComplete; }

Enemy *Wave::checkEnemy(int x, int y, int radius, float freeze)
{
	std::vector<Enemy *> sorted = list;
	std::stable_sort(sorted.begin(), sorted.end(), further);
	for (size_t i = 0; i < sorted.size(); i++)
	{
		Enemy *t = sorted[i];
		if (dist((float)(int)t->position[0], (float)(int)t->position[1], (float)x, (float)y) <= radius && t->alive() && t->position[0] > 0)
			if (freeze == 0 || !t->frozen || (t->freezeCount < (int)millis() - t->freezeTime + 300))
				return t;
	}
	for (size_t i = 0; i < sorted.size(); i++)
	{
		Enemy *t = sorted[i];
		if (dist((float)(int)t->position[0], (float)(int)t->position[1], (float)x, (float)y) <= radius && t->alive() && t->position[0] > 0)
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
Waves::Waves(const std::string &path, Map *m) : map(m)
{
	XNode root;
	if (!xml_load(path + "/waves.xml", root))
		return;
	std::vector<const XNode *> ws = root.list("waves/wave");
	for (size_t i = 0; i < ws.size(); i++)
	{
		std::vector<const XNode *> en;
		for (size_t k = 0; k < ws[i]->kids.size(); k++)
			if (ws[i]->kids[k].name == "enemy")
				en.push_back(&ws[i]->kids[k]);
		waves.push_back(new Wave(ws[i]->num("enemydistance"), en, m));
	}
}
Waves::~Waves() { for (size_t i = 0; i < waves.size(); i++) delete waves[i]; }

void Waves::waveComplete()
{
	if (waves[current]->complete() && !map->defeated)
	{
		if (current + 1 >= (int)waves.size())
			map->wonGame();
		else
		{
			current++;
			map->continueNextWave();
		}
	}
}

/* ------------------------------------------------------------------ tower */
Tower::Tower(const TowerType &t, int x, int y, Map *m) : map(m)
{
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
	for (int i = 0; i < 4; i++)
	{
		ucosts[i] = t.ucosts[i];
		udamage[i] = t.udamage[i];
		ufirerate[i] = t.ufirerate[i];
		ufireradius[i] = t.ufireradius[i];
	}
	enemyPosition[0] = enemyPosition[1] = hitAniPos[0] = hitAniPos[1] = arrowPosition[0] = arrowPosition[1] = 0;
}

std::string Tower::description() const
{
	char b[160];
	snprintf(b, sizeof b, "DMG: %d   FR: %.1fs\n", damage, (float)fireRate / 1000);
	return std::string(b) + desc;
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
		health = maxHealth;
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
		currentUpdate++;
	}
}

void Tower::enemyDamage(int dmg)
{
	health -= dmg;
	if (health < 0 && !destroyed)
	{
		destroyed = true;
		destroyCount = millis();
	}
}

void Tower::checkEnemy()
{
	if (!map->waves)
		return;
	int tile = map->tileSize;
	Enemy *e = map->waves->started ? map->waves->waves[map->waves->current]->checkEnemy((int)((position[0] + 0.5) * tile), (int)((position[1] + 0.5) * tile), (fireRadius / 2) + (towerTileSize / 4), freeze) : NULL;
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
	shootingCount = millis();
	shooting = true;
	enemyLastHit = millis();
}

void Tower::display()
{
	int tile = map->tileSize;
	float cx = (position[0] + 0.5f) * tile, cy = ((position[1] + 0.5f) * tile) - ((towerTileSize - tile) / 2);
	if (!destroyed)
	{
		if (drawingCount >= 5)
		{
			float a = shootAngle + (shootDirection * PI_F);
			if ((int)millis() - enemyLastHit > 3000) towerDirection = 2;
			else if (a > PI_F / 8 && a <= (PI_F * 3 / 8)) towerDirection = 1;
			else if (a > (PI_F * 3 / 8) && a <= (PI_F * 5 / 8)) towerDirection = 0;
			else if (a > (PI_F * 5 / 8) && a <= (PI_F * 7 / 8)) towerDirection = 5;
			else if (a > (PI_F * 7 / 8) && a <= (PI_F + (PI_F / 8))) towerDirection = 6;
			else if ((a > (PI_F + (PI_F / 8)) && a <= (PI_F + (PI_F * 3 / 8))) || (a < -(PI_F * 5 / 8) && a >= -(PI_F * 7 / 8))) towerDirection = 7;
			else if ((a > (PI_F + (PI_F * 3 / 8)) && a <= (PI_F + (PI_F * 5 / 8))) || (a < -(PI_F * 3 / 8) && a >= -(PI_F * 5 / 8))) towerDirection = 4;
			else if ((a > (PI_F + (PI_F * 5 / 8)) && a <= (PI_F + (PI_F * 7 / 8))) || (a < -(PI_F / 8) && a >= -(PI_F * 3 / 8))) towerDirection = 3;
			else towerDirection = 2;
			uint32_t tint = 0xFFFFFF;
			if (selected)   /* the chosen tower pulses while its menu is open */
			{
				int b = 140 + (int)(115 * (0.5f + 0.5f * cosf(millis() / 160.0f)));
				tint = 0xFFFF00 | (b & 255);
			}
			if (shooting && shootingAniState < 3 && hasShootingAnimation)
			{
				frame_center(sheet, (currentUpdate * 9) + shootingAniState + 3, towerDirection, cx, cy, 255, tint);
				if ((int)millis() - shootingCount > fireRate * 0.08f * (shootingAniState + 1) && shootingAniState < 3)
					shootingAniState++;
			}
			else
			{
				if ((float)((int)millis() - animationCount) >= (1000 / (5 * globalSpeed)))
				{
					animationState += animationStateDirection;
					if (animationState == 3 || animationState == -1)
					{
						animationState = 1;
						animationStateDirection *= -1;
					}
					animationCount = millis();
				}
				frame_center(sheet, (currentUpdate * 9) + animationState, towerDirection, cx, cy, 255, tint);
			}
			if ((float)((int)millis() - fireCount) >= (fireRate / globalSpeed))
			{
				checkEnemy();
				fireCount = millis();
			}
		}
		else
			drawingCount++;
	}
	else
	{
		int dt = (int)millis() - destroyCount, state = 8, alpha = 255;
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
		fill_circle(cx, cy, fireRadius / 2.0f, 0xCCFF99, 60);
		circle_outline(cx, cy, fireRadius / 2.0f, 0xCCFF99, 200);
	}
}

void Tower::displayArrow()
{
	if (!shooting)
		return;
	int tile = map->tileSize;
	if ((int)millis() - shootingCount < (fireRate * 0.5f))
	{
		float tx = (position[0] + 0.5f) * tile, ty = ((position[1] + 0.5f) * tile) - ((towerTileSize - tile) / 2);
		float c = dist(tx, ty, enemyPosition[0], enemyPosition[1]);
		float proz = (float)((int)millis() - shootingCount) / (fireRate * 0.7f);
		float ang = shootAngle + (shootDirection * PI_F);
		arrowPosition[0] = cosf(ang) * (c - (proz * c));
		arrowPosition[1] = sinf(ang) * (c - (proz * c));
		int alpha = ((proz < 0.10f || proz > 0.95f)) ? 80 : 255;
		if (proz > 0.05f && frame_ok(sheet, (currentUpdate * 9) + 6 + animationState, 2))
			draw_image_rot(sheet->img, ((currentUpdate * 9) + 6 + animationState) * sheet->tile, 2 * sheet->tile, sheet->tile, sheet->tile,
			               enemyPosition[0] + arrowPosition[0] - camX, enemyPosition[1] + arrowPosition[1] - camY, ang, alpha);
	}
	else
	{
		if (shootAt)
		{
			if (damageIncrease > 0)
				shootAt->hitByTower(damage, freeze, this, damageIncrease);
			else
				shootAt->hitByTower(damage, freeze);
			if (splash && map->waves)
				map->waves->waves[map->waves->current]->splash(shootAt, damage, splashRadius, splashFactor);
			if (explosionAtEnemy)
				shootAt->hitAnimation(sheetFile, 6 + currentUpdate * 9);
			else
			{
				hitAniCount = millis();
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
	int dt = (int)millis() - hitAniCount;
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

Map::Map(const std::string &mapPath, int diff) : path(mapPath), difficulty(diff), charSets(mapPath)
{
	g_map = this;
	size_t slash = mapPath.rfind('/');
	id = mapPath.substr(slash == std::string::npos ? 0 : slash + 1);
	if (id.size() > 4 && id.compare(id.size() - 4, 4, ".xml") == 0)
		id.resize(id.size() - 4);
	grade_set_map(id.c_str());
	globalSpeed = 1.0f;
	camX = camY = 0;

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

	if (!load_image565(path + "/data/map.png", mapImage))
		return;
	load_image("data/hud_top.png", hudTop);
	load_image("data/hud_bottom.png", hudBottom);
	load_image("data/money.png", moneyImg);
	load_image("data/life.png", lifeImg);
	load_image("data/hud_pause.png", hudPause);
	load_image("data/hud_play.png", hudPlay);
	load_image("data/build.png", hudBuild);
	load_image("data/menu.png", hudMenu);
	if (optHQ)
		load_image("data/ray1.png", lightRay);
	waves = new Waves(path, this);
	ok = !waves->waves.empty();
	startMessage();
}

Map::~Map()
{
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
		towers.push_back(new Tower(t, tx, ty, this));
	}
}

void Map::sellTower(Tower *t)
{
	money += (int)(t->costs * 0.8);
	removeTower(t);
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

void Map::checkTowers(int x, int y, int radius, int damage)
{
	for (size_t i = 0; i < towers.size(); i++)
		if (dist((towers[i]->position[0] + 0.5f) * tileSize, (towers[i]->position[1] + 0.5f) * tileSize, (float)x, (float)y) <= radius)
			towers[i]->enemyDamage(damage);
}

struct DrawItem { float y; Tower *tower; Enemy *enemy; };
static bool by_y(const DrawItem &a, const DrawItem &b) { return a.y < b.y; }

void Map::display()
{
	blit565_window(mapImage, camX, camY);
	if (showBuildAreas)
		for (int i = 0; i < cols; i++)
			for (int j = 0; j < rows; j++)
			{
				int k = tileType[(size_t)i * rows + j];
				if (k != 1 && k != 3 && k != 4 && k != 5)
				{
					int x = i * tileSize - camX, y = j * tileSize - camY;
					if (x + tileSize > 0 && y + tileSize > 0 && x < SW && y < SH)
						fill_rect(x + 1, y + 1, tileSize - 2, tileSize - 2, 0x339933, 110);
				}
			}
	/* towers and enemies, back to front */
	std::vector<DrawItem> items;
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
		}
		waves->waveComplete();
		w->calcEnemyDistance();
	}
	std::stable_sort(items.begin(), items.end(), by_y);
	for (size_t i = 0; i < items.size(); i++)
	{
		if (items[i].tower) items[i].tower->display();
		else items[i].enemy->display();
	}
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
}

/* ------------------------------------------------------------------ flow and the on-screen messages */
void Map::startMessage() { actionTimer = millis(); action = 5; }
void Map::startGame() { gameStarted = true; actionTimer = millis(); action = 1; }
void Map::continueNextWave() { actionTimer = millis(); action = 2; }
void Map::wonGame() { actionTimer = millis(); action = 3; globalSpeed = 0; defeated = true; }
void Map::loseGame() { actionTimer = millis(); action = 4; defeated = true; globalSpeed = 0; }

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
			if (dt <= 3000) big_text("Prepare!", 34, -20);
			else if (dt <= 4000) big_text("3", 52, -30);
			else if (dt <= 5000) big_text("2", 52, -30);
			else if (dt <= 6000) big_text("1", 52, -30);
			else big_text("GO!", 56, -34);
		}
		else
		{
			waves->nextWave();
			action = 0;
		}
		break;
	case 2:
		if (dt >= 2000)
		{
			waves->nextWave();
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
	/* money and lives, top left */
	draw_image(hudTop, 0, 0);
	draw_image(moneyImg, 8, 9);
	char b[32];
	snprintf(b, sizeof b, "%d", money);
	draw_text(b, 26, 8, 13, TEXT_GOLD, ALIGN_LEFT, 2, 255, 0x000000);
	draw_image(lifeImg, 70, 11);
	snprintf(b, sizeof b, "%d", lifes);
	draw_text(b, 88, 8, 13, TEXT_GOLD, ALIGN_LEFT, 2, 255, 0x000000);
	/* wave information, bottom right */
	draw_image(hudBottom, SW - 400, SH - 50);
	snprintf(b, sizeof b, "Wave %d of %d - next wave:", waves->current + 1, (int)waves->waves.size());
	draw_text(b, SW - 358, SH - 30, 11, TEXT_GOLD, ALIGN_LEFT, 2, 255, 0x000000);
	if (waves->current + 1 < (int)waves->waves.size())
	{
		Wave *nw = waves->waves[waves->current + 1];
		for (size_t i = 0; i < nw->nextIcons.size(); i++)
			if (nw->nextIcons[i])
				draw_image(nw->nextIcons[i]->img, SW - 165 + (int)i * 32, SH - 30);
	}
	/* the three buttons, bottom left, each with the pad button that works it */
	draw_image(pause ? hudPlay : hudPause, 13, SH - 49);
	draw_image(hudBuild, 65, SH - 47);
	draw_image(hudMenu, 105, SH - 47);
	draw_text("START", 31, SH - 13, 10, 0xFFFFFF, ALIGN_CENTER, 2, 255, 0x000000);
	draw_text("X", 76, SH - 13, 10, 0xFFFFFF, ALIGN_CENTER, 2, 255, 0x000000);
	draw_text("SELECT", 119, SH - 13, 10, 0xFFFFFF, ALIGN_CENTER, 2, 255, 0x000000);
}
