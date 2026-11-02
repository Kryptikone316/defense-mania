/* Defense Mania for the Data Frog SF3500 - game classes (a C++ port of the Processing sketch). */
#ifndef DM_GAME_HPP
#define DM_GAME_HPP

#include "gfx.hpp"
#include "xml.hpp"
#include <map>
#include <string>
#include <vector>

extern float globalSpeed;
extern bool optShowFps, optHQ;
extern int camX, camY;

struct Sheet {
	Image img;
	int tile = -1;
	int cols() const { return tile > 0 ? img.w / tile : 1; }
	int rows() const { return tile > 0 ? img.h / tile : 1; }
};

class Map;

/* what happened in a match, for the sheet shown at the end */
struct Stats {
	int goldEarned = 0, goldSpent = 0, goldSold = 0;
	int built[8] = { 0 }, upgrades = 0, repairs = 0, sold = 0, towersLost = 0;
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
	Enemy(const XNode &def, Map *map);
	void display();
	void displayGui();
	void setWaypoints(const int *start, const int *end, const std::vector<std::pair<int, int> > &wps);
	void hitByTower(int damage, float freeze);
	void hitByTower(int damage, float freeze, Tower *tower, float multiplier);
	void hitAnimation(const std::string &charset, int x);
	bool alive() const { return enemyAlive; }
	void calcFuture(int timeMs, float out[2]) const;

	float moveComplete = 0, position[2];
	bool enemyAlive = true, enemyDied = false, wayPointsSet = false, frozen = false;
	int tileSize, moveDirection = 2, freezeCount = 0, freezeTime = 0;
	Sheet *icon = NULL;
private:
	void checkWP();
	void calculateMoveVector();
	void getDamage(int damage, float freeze);
	Map *map;
	Sheet *sheet;
	std::string sheetFile;
	std::vector<TowerHit> towerHits;
	int healthBarOffset = -1, animationCount = 0, moveCount = 0, moveState = 1, moveStateDirection = 1, moveVector[2];
	float animationSpeed;
	int moveSpeed, money, damage, maxHealth, health;
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
	Wave(int distance, const std::vector<const XNode *> &enemies, Map *map);
	~Wave();
	void start();
	void calcEnemyDistance();
	void displayGui();
	void splash(Enemy *e, int damage, int radius, float factor);
	Enemy *checkEnemy(int x, int y, int radius, float freeze);
	bool complete() const;
	std::vector<Enemy *> list;
	std::vector<Sheet *> nextIcons;
private:
	int enemyDistance, wholeDistance = 0, offset = 500;
	bool starting = false;
};

class Waves {
public:
	Waves(const std::string &path, Map *map);
	~Waves();
	void nextWave() { started = true; waves[current]->start(); }
	void waveComplete();
	void displayGui() { if (started) waves[current]->displayGui(); }
	std::vector<Wave *> waves;
	int current = 0;
	bool started = false;
private:
	Map *map;
};

struct TowerType {
	std::string name, charset, icon, description;
	int tileSize, iconSize, damage, fireRate, fireRadius, splashRadius, costs, maxHealth, updates;
	bool splash, explosionAtEnemy, hasShootingAnimation;
	float splashFactor, freeze, damageIncrease;
	int ucosts[4], udamage[4], ufirerate[4], ufireradius[4];
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
	int costs, health, maxHealth, currentUpdate = 0, updates, towerTileSize;
	int udamage[4], ucosts[4];
	bool destroyed = false, showRadius = false, selected = false;
	std::string name;
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

class Map {
public:
	Map(const std::string &mapPath, int difficulty);
	~Map();
	void display();
	bool canBuild(int x, int y) const;
	int towerAt(int x, int y) const;
	void addTower(int type, int tx, int ty);
	void sellTower(Tower *t);
	void removeTower(Tower *t);
	void checkTowers(int x, int y, int radius, int damage);

	std::string path, id;
	int difficulty, tileSize, mapW, mapH, cols, rows, money, lifes;
	Image565 mapImage;
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
	bool ok = false;

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
