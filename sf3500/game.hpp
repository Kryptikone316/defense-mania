/* Defense Mania for the Data Frog SF3500 - game classes (a C++ port of the Processing sketch). */
#ifndef DM_GAME_HPP
#define DM_GAME_HPP

#include "gfx.hpp"
#include "xml.hpp"
#include <map>
#include <string>
#include <vector>

extern float globalSpeed;
extern int g_prof[5], g_profEnemies, g_profTowers;
uint32_t gnow();   /* the game clock, see game.cpp */
void game_clock_reset();
extern bool optShowFps, optHQ;
extern int camX, camY;

struct Sheet {
	Image img;
	int tile = -1;
	int cols() const { return tile > 0 ? img.w / tile : 1; }
	int rows() const { return tile > 0 ? img.h / tile : 1; }
};

class Map;

/* how much tougher the creatures of a wave are than the map file says (endless mode) */
struct WaveScale {
	float hp = 1, speed = 1, count = 1, money = 1, damage = 1;
	float gold = 1;   /* the gold a kill pays (the score still counts the plain money value) */
};

/* what happened in a match, for the sheet shown at the end */
struct Stats {
	int goldEarned = 0, goldSpent = 0, goldSold = 0;
	int built[8] = { 0 }, upgrades = 0, repairs = 0, sold = 0, towersLost = 0;
	int trapsBuilt[4] = { 0 }, boulderThrows = 0;   /* by trap type; boulders thrown back at towers */
	float depotGold = 0;                            /* the extra gold the Resupply Depots paid */
	int enemiesDefeated = 0, enemiesLeaked = 0, shots = 0;
	int playMs = 0;
};

class CharSets {
public:
	explicit CharSets(const std::string &path) : mapPath(path) {}
	Sheet *add(const std::string &file, int tile);
	Sheet *get(const std::string &file);
private:
	std::string mapPath;
	std::map<std::string, Sheet> sets;
};

class Tower;

struct TowerHit {
	Tower *tower;
	int count, damage;
	float multiplier;
	int next() { int d = damage; for (int i = 0; i < count; i++) d = (int)(d * multiplier); count++; return d; }
};

class Enemy {
public:
	Enemy(const XNode &def, Map *map, const WaveScale &scale);
	void display();
	void displayGui();
	void setWaypoints(const int *start, const int *end, const std::vector<std::pair<int, int> > &wps);
	void hitByTower(float damage, float freeze, float chill = 0);
	void hitByTower(float damage, float freeze, Tower *tower, float multiplier, float chill = 0);
	void hitAnimation(const std::string &charset, int x);
	bool alive() const { return enemyAlive; }
	bool flying = false; /* ground-only towers cannot target it, ground traps do not touch it */

	int maxHealthValue() const { return maxHealth; }
	void calcFuture(int timeMs, float out[2]) const;

	float armor = 0;         /* flat damage taken off every hit (never more than three quarters of it) */
	bool thrower = false;    /* lifts a blocking boulder and hurls it at a tower nearby */
	float throwRange = 150, throwDamage = 100;
	int liftTime = 2500, liftStart = 0, liftTrapId = 0;
	int glueUntil = 0;       /* stuck in glue until this game time */
	float glueFactor = 1;    /* ... and moving at this fraction of the speed */
	float moveComplete = 0, position[2];
	bool enemyAlive = true, enemyDied = false, wayPointsSet = false, frozen = false;
	int tileSize, moveDirection = 2, freezeCount = 0, freezeTime = 0;
	Sheet *icon = NULL;
private:
	void checkWP();
	void calculateMoveVector();
	void getDamage(float damage, float freeze, float chill);
	float dmgCarry = 0, chillPct = 0;    /* the fractions of damage not dealt yet; how much more damage a slowed creature takes */
	int chillUntil = 0;
	Map *map;
	Sheet *sheet;
	std::string sheetFile;
	std::vector<TowerHit> towerHits;
	int healthBarOffset = -1, animationCount = 0, moveCount = 0, moveState = 1, moveStateDirection = 1;
	float moveVector[2], target[2];   /* a unit vector towards the next way point */
	float animationSpeed;
	int moveSpeed, money, damage, maxHealth, health;
	float goldValue = 0;   /* what a kill pays out: money x the gold factor of the difficulty and the wave */
	int start[2], end[2];
	std::vector<std::pair<int, int> > way;
	int wpPosition = 0;
	bool reachingEndpoint = false;
	int fireCount = 0, fireRate = 1000, fireRadius = 50;
	float freezeFactor = 0, freezeFactorTemp = 1;
	int dieCount = 0, dieStep = 0, dieTint = 255;
	bool gotHit = false;
	int hitCount = 0, hitX = 0;
	std::string hitCharset;
};

class Wave {
public:
	Wave(int distance, const std::vector<const XNode *> &enemies, Map *map, const WaveScale &scale);
	~Wave();
	void start();
	void calcEnemyDistance();
	void displayGui();
	void splash(Enemy *e, float damage, int radius, float factor);
	Enemy *checkEnemy(int x, int y, int radius, float freeze, bool groundOnly = false);
	bool complete() const;
	std::vector<Enemy *> list;
	std::vector<Sheet *> nextIcons;
private:
	int enemyDistance, wholeDistance = 0, offset = 500;
	bool starting = false;
};

class Waves {
public:
	Waves(const std::string &path, Map *map, bool endless);
	~Waves();
	void nextWave() { started = true; waves[current]->start(); ensureNext(); }
	void ensureNext();   /* endless: the wave after the current one is always made ready */
	void waveComplete();
	void displayGui() { if (started) waves[current]->displayGui(); }
	std::vector<Wave *> waves;
	int current = 0;
	bool started = false;
private:
	Map *map;
	bool endless;
	XNode root;
	std::vector<std::vector<const XNode *> > templates;
	std::vector<int> spacing;
};

/* what a Resupply Depot gives the towers (and kills) inside its area, per level */
struct SupportLevel {
	float range = 0;      /* pixels from the depot's centre */
	float rate = 0;       /* percent more shots */
	float heal = 0;       /* percent of a tower's health mended each second */
	float gold = 0;       /* gold more for every creature that dies inside the area */
	float goldPct = 0;    /* percent more kill gold (on top of the flat gold) */
};
std::string support_text(const SupportLevel &s, int tile);

struct TowerType {
	std::string name, charset, icon, description;
	int tileSize, iconSize, damage, fireRate, fireRadius, splashRadius, costs, maxHealth, updates;
	bool splash, explosionAtEnemy, hasShootingAnimation;
	float splashFactor, freeze, damageIncrease;
	int ucosts[4], udamage[4], ufirerate[4], ufireradius[4];
	bool support = false;   /* a depot: no weapon, it supplies the towers around it */
	SupportLevel sup[3];
	bool groundOnly = false;   /* artillery: cannot hit flying creatures */
	float chill[3] = { 0, 0, 0 };   /* Freeze towers: percent more damage every tower deals to a slowed creature */
	Image iconImg;
};

class Tower {
public:
	Tower(const TowerType &t, int x, int y, Map *map);
	void display();
	void displayGui();
	void displayArrow();
	void hitAnimation();
	void enemyDamage(int damage);
	std::string description() const;
	int repairCosts() const;
	void repair();
	void upgrade();
	int position[2];
	int drawnFrame = -1, dbgCol = -1, dbgRow = -1;   /* for the ghost hunt: the last frame this tower was drawn in, and with which picture */
	const Sheet *sheetPtr() const { return sheet; }
	void center(float &x, float &y) const;
	std::string debugState(bool withName = true) const;
	int costs, health, maxHealth, currentUpdate = 0, updates, towerTileSize;
	int udamage[4], ucosts[4];
	bool destroyed = false, showRadius = false, selected = false;
	std::string name;
	bool support = false;   /* a Resupply Depot */
	SupportLevel sup[3];
	bool groundOnly = false;
	float chill[3] = { 0, 0, 0 };
	float chillNow() const { return chill[currentUpdate < 3 ? currentUpdate : 2]; }
	const SupportLevel &supportNow() const { return sup[currentUpdate < 3 ? currentUpdate : 2]; }
	float buffRate = 1.0f, healCarry = 0;   /* set every frame by the map from the depots around */
private:
	void checkEnemy();
	void shoot(float x, float y);
	Map *map;
	Sheet *sheet;
	std::string sheetFile, desc;
	int damage, fireRate, fireRadius, splashRadius;
	int ufirerate[4], ufireradius[4];
	bool splash, explosionAtEnemy, hasShootingAnimation;
	float splashFactor, freeze, damageIncrease;
	int drawingCount = 0, towerDirection = 3, fireCount = 0, animationCount = 0, animationState = 0, animationStateDirection = 1;
	float shootAngle = 0;
	bool shooting = false, hitAniBool = false;
	int shootDirection = 0, shootingCount = 0, shootingAniState = 0, enemyLastHit = 0, hitAniCount = 0, destroyCount = 0, destroyOpacity = 255;
	float enemyPosition[2], hitAniPos[2], arrowPosition[2];
	Enemy *shootAt = NULL;
};

/* path traps, set on the path by the player: Spikes hurt what crosses them, Glue slows, a Boulder blocks the path until it is smashed */
struct Projectile {   /* a thrown boulder in the air */
	float x0, y0, x1, y1;
	int start, duration;
	float damage;
};

struct TrapType {
	std::string id, name, kind, text;
	int costs = 0, cap = 0, charges = 0, interval = 0, duration = 0, health = 0, attackers = 0;
	float radius = 0, damage = 0, slow = 1;
	Image icon, picture, crack[2];   /* a boulder shows cracks as it is smashed */
};
struct Trap {
	int id = 0;
	int type = 0, born = 0;
	float x = 0, y = 0;
	float charges = 0, health = 0;   /* spikes: hits left; boulder: health left */
	int attackers = 0;               /* creatures smashing it this frame */
	std::vector<std::pair<Enemy *, int> > hits;   /* spikes: when each creature was last hurt */
};

class Map {
public:
	Map(const std::string &mapPath, int difficulty, bool endless = false);
	~Map();
	void display();
	bool canBuild(int x, int y) const;
	int towerAt(int x, int y) const;
	void addTower(int type, int tx, int ty);
	void sellTower(Tower *t);
	void removeTower(Tower *t);
	void checkTowers(int x, int y, int radius, int damage);
	std::vector<TrapType> trapTypes;
	std::vector<Trap> traps;
	std::vector<Projectile> projectiles;
	int trapSerial = 0;
	bool hasThrowers() const;
	bool towerInReach(float x, float y, float range) const;
	bool throwBoulder(Trap *t, float range, float damage);   /* false when no tower is in reach */
	std::vector<std::pair<float, float> > pathPts;   /* the path as a line: start, way points, end */
	bool nearPath(float wx, float wy, float maxDist, float &px, float &py) const;
	int trapCount(int type) const;
	std::string trapProblem(int type, float px, float py) const;   /* "" when it can be placed */
	bool addTrap(int type, float px, float py);
	void updateTraps();
	void drawTraps();
	Trap *blockingTrap(float x, float y, float mx, float my, float step);
	void applySupport();   /* every frame: the depots' faster fire and healing for the towers in their areas */
	float supportGold(float x, float y, float killGold) const;   /* the extra gold a kill at this spot pays */
	int supportTick = -1;

	std::string path, id;
	int difficulty, tileSize, mapW, mapH, cols, rows, money, lifes;
	float moneyCarry = 0;   /* the fractions of gold not paid out yet, so small rewards still add up exactly */
	int award(float gold);   /* pays out gold, returns the whole gold given */
	Image565 mapImage, areasImage;   /* areasImage: the map with the build areas baked in, made the first time they are shown */
	Image lightRay, hudTop, hudBottom, moneyImg, lifeImg, hudPause, hudPlay, hudBuild, hudMenu;
	std::vector<int> tileType;   /* cols*rows */
	CharSets charSets;
	std::vector<TowerType> towerTypes;
	std::vector<Tower *> towers;
	Waves *waves = NULL;
	int start[2], end[2];
	std::vector<std::pair<int, int> > way;
	bool gameStarted = false, defeated = false, pause = false, showBuildAreas = false, quitDialog = false;
	int action = 0, actionTimer = 0;
	int killPoints = 0, wavesCleared = 0;   /* for the score */
	Stats stats;
	float scoreMult() const { return difficulty == 1 ? 1.0f : difficulty == 2 ? 1.5f : 2.0f; }
	int runningScore() const { return (int)((killPoints + wavesCleared * 100) * scoreMult()); }
	bool ok = false, endless = false;

	/* game flow */
	void startMessage();
	void startGame();
	void loseGame();
	void wonGame();
	void continueNextWave();
	void displayActions();
	bool finished = false, finishedWon = false;
	void drawHud();
};

#endif
