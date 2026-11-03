/* Display test for the SF3500 build of Defense Mania (started with DM_DISPTEST=1, see DefensemaniaDisp).
 *
 * Question it answers: does what the game hands to the display driver come out of the driver unchanged? A rare "ghost"
 * (a tower that vanishes for two frames and shows up elsewhere) was filmed on the device. The driver copies every frame
 * into its own queue (video_driver_disp_frame -> fbdev_draw_frame) and render threads put it through the graphics engine
 * into /dev/fb0. A dump of /dev/fb0 taken during the first read-back test showed what is in there: a ring of 36 slots of
 * 640 x 240 pixels (18 bands of 240 rows, two slots side by side in the 1280-pixel rows), each holding one frame, rotated
 * and squeezed by the graphics engine. The same input must always give the same slot, so this test
 *   1. times every present call under several pacings,
 *   2. sends two game-like scenes (the Maze map, towers, an enemy, the cursor, the HUD) A and B, each long enough to fill
 *      the whole ring, and keeps one slot of each as the reference picture (and checks that all 36 slots agree),
 *   3. then sends A and B in several rhythms (25 ms, 16.7 ms, bursts, jitter, slow, free-running) while a reader thread
 *      keeps looking at all 36 slots: every slot must look like A or B. A slot that is neither (re-read to rule out a
 *      write in progress) is the artifact: it is logged with the part of the picture that differs and saved to a file.
 * It runs the whole series with music playing and once more without. Select + Start leaves at any time. */
#include "audio.hpp"
#include "gfx.hpp"
#include "hwdisp.hpp"
#include "joykey.hpp"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include <vector>

static uint64_t now_us()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
}

static void wait_until(uint64_t t)
{
	for (;;)
	{
		uint64_t n = now_us();
		if (n >= t)
			return;
		if (t - n > 3000)
			usleep((unsigned)(t - n - 2000));
	}
}

static bool g_abort;
static bool abort_requested()
{
	if (g_abort)
		return true;
	unsigned pad = gfx_pad();
	if (((pad >> JK_START) & 1) && ((pad >> JK_SELECT) & 1))
	{
		g_abort = true;
		printf("DISP stopped with Select+Start\n");
		fflush(stdout);
	}
	return g_abort;
}

/* ------------------------------------------------------------------ /dev/fb0 and its slots */
struct Fb
{
	int fd = -1;
	fb_var_screeninfo v;
	fb_fix_screeninfo f;
	const uint8_t *map = NULL;
	size_t len = 0;
	bool ok = false;
};
static Fb g_fb;

/* slot geometry found in the dump (see the top of the file) */
static const int SLOT_W = 640, SLOT_H = 240, SLOT_COLS = 2, SLOT_BANDS = 18, SLOTS = SLOT_COLS * SLOT_BANDS;
static const int ROW_STEP = 3, WORD_STEP = 7;   /* every 3rd row, every 7th 32-bit word of it */
static size_t g_lineBytes = 2560;

/* Self-check without a device (DM_DISPTEST_FAKEFB=1, for the emulator): a fake /dev/fb0 with the same slot ring, filled
 * from the canvas, with a block displaced now and then, to prove that the reader and the anomaly report work. */
static uint8_t *g_fakeFb;
static bool g_fake;
static int g_fakeCount;

static void fake_after_present(const uint16_t *canvas)
{
	if (!g_fake)
		return;
	int slot = g_fakeCount % SLOTS;
	int x0 = (slot % SLOT_COLS) * SLOT_W, y0 = (slot / SLOT_COLS) * SLOT_H;
	for (int y = 0; y < SLOT_H; y++)   /* a squeezed copy of the canvas */
	{
		const uint16_t *src = canvas + (size_t)(y * SH / SLOT_H) * SW;
		memcpy(g_fakeFb + (size_t)(y0 + y) * g_lineBytes + (size_t)x0 * 2, src, (size_t)(SW < SLOT_W ? SW : SLOT_W) * 2);
	}
	if (++g_fakeCount % 97 == 0)   /* the glitch: a 64 x 64 block copied to another place in the slot */
		for (int y = 0; y < 64; y++)
			memcpy(g_fakeFb + (size_t)(y0 + 60 + y) * g_lineBytes + (size_t)(x0 + 400) * 2, g_fakeFb + (size_t)(y0 + 60 + y) * g_lineBytes + (size_t)(x0 + 80) * 2, 64 * 2);
}

static void present_frame()
{
	uint16_t *c = NULL;
	if (g_fake)
	{
		unsigned long cv[3];
		gfx_canvas_info(cv);
		c = (uint16_t *)cv[0];
	}
	gfx_present();
	fake_after_present(c);
}

static void fb_probe()
{
	if (getenv("DM_DISPTEST_FAKEFB"))
	{
		g_fake = true;
		g_fakeFb = (uint8_t *)calloc(1, g_lineBytes * 4320);
		memset(&g_fb.v, 0, sizeof g_fb.v);
		memset(&g_fb.f, 0, sizeof g_fb.f);
		g_fb.map = g_fakeFb;
		g_fb.len = g_lineBytes * 4320;
		g_fb.ok = true;
		printf("DISP using a FAKE frame buffer (self-check)\n");
		return;
	}
	for (int i = 0; i < 4; i++)
	{
		char path[32];
		snprintf(path, sizeof path, "/dev/fb%d", i);
		int fd = open(path, O_RDWR);
		if (fd < 0)
			fd = open(path, O_RDONLY);
		if (fd < 0)
		{
			printf("DISP %s: cannot open (%s)\n", path, strerror(errno));
			continue;
		}
		fb_var_screeninfo v;
		fb_fix_screeninfo f;
		memset(&v, 0, sizeof v);
		memset(&f, 0, sizeof f);
		int r1 = ioctl(fd, FBIOGET_VSCREENINFO, &v), r2 = ioctl(fd, FBIOGET_FSCREENINFO, &f);
		printf("DISP %s: id '%s' vscreen %s fscreen %s\n", path, f.id, r1 ? "failed" : "ok", r2 ? "failed" : "ok");
		if (!r1)
			printf("DISP   res %ux%u virtual %ux%u offset %u,%u bpp %u line %u bytes  smem_len %u\n", v.xres, v.yres, v.xres_virtual, v.yres_virtual, v.xoffset, v.yoffset, v.bits_per_pixel, f.line_length, f.smem_len);
		if (i == 0 && !r1 && !r2 && f.smem_len > 0 && v.bits_per_pixel == 16)
		{
			size_t len = f.smem_len > (16u << 20) ? (16u << 20) : f.smem_len;
			void *m = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
			if (m == MAP_FAILED)
				printf("DISP   mmap of %s failed (%s)\n", path, strerror(errno));
			else
			{
				g_fb.fd = fd;
				g_fb.v = v;
				g_fb.f = f;
				g_fb.map = (const uint8_t *)m;
				g_fb.len = len;
				g_fb.ok = true;
				g_lineBytes = f.line_length;
				printf("DISP   mapped %zu bytes of %s for reading\n", len, path);
				continue;   /* keep fd open */
			}
		}
		close(fd);
	}
	if (g_fb.ok && g_lineBytes * (size_t)(SLOT_BANDS * SLOT_H) > g_fb.len)
	{
		printf("DISP   the mapping is too small for the 36-slot layout: read-back off\n");
		g_fb.ok = false;
	}
	fflush(stdout);
}

/* A slot's samples: every ROW_STEP-th row, every WORD_STEP-th word of it. */
struct Slot
{
	std::vector<uint32_t> w;
};
static const int WORDS_PER_ROW = SLOT_W / 2;   /* two 16-bit pixels per word */
static const int SAMPLES_PER_ROW = (WORDS_PER_ROW + WORD_STEP - 1) / WORD_STEP;

static void slot_read(int slot, Slot &s)
{
	int x0 = (slot % SLOT_COLS) * SLOT_W, y0 = (slot / SLOT_COLS) * SLOT_H;
	s.w.resize((size_t)(SLOT_H / ROW_STEP) * SAMPLES_PER_ROW);
	size_t n = 0;
	for (int ry = 0; ry < SLOT_H; ry += ROW_STEP)
	{
		const uint32_t *row = (const uint32_t *)(g_fb.map + (size_t)(y0 + ry) * g_lineBytes + (size_t)x0 * 2);
		for (int k = 0; k < WORDS_PER_ROW; k += WORD_STEP)
			s.w[n++] = row[k];
	}
}

static size_t slot_diff(const Slot &a, const Slot &b)
{
	size_t d = 0, n = a.w.size() < b.w.size() ? a.w.size() : b.w.size();
	for (size_t i = 0; i < n; i++)
		d += a.w[i] != b.w[i];
	return d;
}

/* ------------------------------------------------------------------ scenes (drawn with the game's own routines) */
static Image565 g_map;
static Image g_towers, g_freeze, g_enemy;
static bool g_assets;

static bool load_assets()
{
	if (g_assets)
		return true;
	g_assets = load_image565("data/maps/maze.xml/data/map.png", g_map) && load_image("data/maps/maze.xml/data/tower1.png", g_towers) &&
	           load_image("data/maps/maze.xml/data/tower3.png", g_freeze) && load_image("data/maps/maze.xml/data/charset1.png", g_enemy);
	return g_assets;
}

/* scene 0 = A, 1 = B: B differs from A by a tower's animation frame, an enemy's step, the cursor and a number */
static void draw_scene(int which)
{
	blit565_window(g_map, 0, 0);
	static const int tx[7] = { 1, 2, 3, 1, 2, 3, 4 }, ty[7] = { 1, 1, 1, 3, 3, 3, 3 };
	for (int i = 0; i < 7; i++)
	{
		const Image &sheet = i == 1 ? g_freeze : g_towers;
		int col = (which && i == 1) ? 1 : 0;
		draw_image(sheet, tx[i] * 48 + 24 - 32, ty[i] * 48 + 24 - 40, col * 64, 2 * 64, 64, 64);
	}
	int ex = 20 + (which ? 6 : 0);
	draw_image(g_enemy, ex, 5 * 48 + 24 - 40, (which ? 2 : 1) * 64, 2 * 64, 64, 64);
	int cx = (which ? 3 : 2) * 48, cy = 1 * 48;
	rect_outline(cx, cy, 48, 48, 0xFFE066, 190);
	draw_line(cx + 16, cy + 24, cx + 32, cy + 24, 0xFFFFFF);
	draw_line(cx + 24, cy + 16, cx + 24, cy + 32, 0xFFFFFF);
	if (which)
		draw_image_rot(g_towers, 6 * 64, 2 * 64, 64, 64, 200, 150, 0.6f, 255);
	char b[48];
	snprintf(b, sizeof b, "$ %d   SCORE %d", 285 + which, 1750 + which * 10);
	draw_text(b, 8, 8, 13, 0xFFE066, ALIGN_LEFT, 2, 255, 0x000000);
	draw_text("DISPLAY TEST   Select+Start: exit", SW / 2, SH - 16, 10, 0xFFFFFF, ALIGN_CENTER, 2, 255, 0x000000);
}

/* ------------------------------------------------------------------ the references */
static Slot g_ref[2];
static bool g_haveRef;
static int g_dumps;

static void save_slot(const char *tag, int slot, int idx)
{
	if (g_dumps >= 8)
		return;
	g_dumps++;
	char name[96];
	snprintf(name, sizeof name, "dbg/disp_anomaly_%d_%s.raw", idx, tag);
	FILE *f = fopen(name, "wb");
	if (!f)
		return;
	int x0 = (slot % SLOT_COLS) * SLOT_W, y0 = (slot / SLOT_COLS) * SLOT_H;
	for (int ry = 0; ry < SLOT_H; ry++)
		fwrite(g_fb.map + (size_t)(y0 + ry) * g_lineBytes + (size_t)x0 * 2, 1, (size_t)SLOT_W * 2, f);
	fclose(f);
	printf("DISP   saved slot %d to %s (%d x %d pixels, 16 bpp)\n", slot, name, SLOT_W, SLOT_H);
}

/* send scene `which` often enough to fill the whole ring */
static void settle(int which)
{
	for (int i = 0; i < SLOTS + 8 && !g_abort; i++)
	{
		draw_scene(which);
		present_frame();
		usleep(30000);
	}
	usleep(150000);
}

static bool capture_refs()
{
	g_haveRef = false;
	for (int w = 0; w < 2; w++)
	{
		settle(w);
		Slot first, other;
		slot_read(0, first);
		int same = 0;
		size_t worst = 0;
		for (int s = 0; s < SLOTS; s++)
		{
			slot_read(s, other);
			size_t d = slot_diff(first, other);
			same += d == 0;
			if (d > worst) worst = d;
		}
		printf("DISP reference %c: %d of %d slots are identical to slot 0 (worst slot differs in %zu of %zu samples)\n", 'A' + w, same, SLOTS, worst, first.w.size());
		g_ref[w] = first;
	}
	size_t ab = slot_diff(g_ref[0], g_ref[1]);
	printf("DISP scene A and scene B differ in %zu of %zu samples of a slot%s\n", ab, g_ref[0].w.size(), ab == 0 ? "  -> the slots do not show the difference: no read-back possible" : "");
	g_haveRef = ab > 0;
	fflush(stdout);
	return g_haveRef;
}

/* ------------------------------------------------------------------ test runs */
struct Stat
{
	int n = 0;
	double sum = 0, mx = 0, mn = 1e9;
	int over5 = 0, over10 = 0, over20 = 0;
	void add(double ms)
	{
		n++; sum += ms; if (ms > mx) mx = ms; if (ms < mn) mn = ms;
		over5 += ms > 5; over10 += ms > 10; over20 += ms > 20;
	}
};

struct Pattern
{
	const char *name;
	int frames;
	int kind;   /* 0 alternate A,B at paceUs; 1 random A/A/A/B at paceUs; 2 bursts; 3 jitter; 4 free run */
	int paceUs;
};

/* The reader runs on a thread of its own and looks at all 36 slots over and over while the main thread presents
 * frames at the pace of the game. */
struct Reader
{
	volatile bool stop = false;
	volatile int frameNo = 0, lastScene = 0;
	long slotReads = 0, asA = 0, asB = 0, transient = 0, anomalies = 0, scans = 0;
	double scanMs = 0, scanMax = 0;
	const char *name = "";
};

static void *reader_main(void *arg)
{
	Reader *r = (Reader *)arg;
	Slot s, again;
	uint32_t lastBad[SLOTS] = {};   /* hash of the bad picture last reported per slot: a bad slot stays bad until it is overwritten */
	while (!r->stop)
	{
		uint64_t t0 = now_us();
		int frame = r->frameNo, scene = r->lastScene;
		for (int slot = 0; slot < SLOTS && !r->stop; slot++)
		{
			slot_read(slot, s);
			size_t dA = slot_diff(s, g_ref[0]), dB = slot_diff(s, g_ref[1]);
			r->slotReads++;
			if (dA == 0) { r->asA++; continue; }
			if (dB == 0) { r->asB++; continue; }
			usleep(2500);   /* a write may be in progress: look again */
			slot_read(slot, again);
			size_t eA = slot_diff(again, g_ref[0]), eB = slot_diff(again, g_ref[1]);
			if (eA == 0 || eB == 0)
			{
				r->transient++;
				continue;
			}
			uint32_t h = 2166136261u;
			for (size_t j = 0; j < again.w.size(); j++)
				h = (h ^ again.w[j]) * 16777619u;
			if (h == lastBad[slot])
				continue;   /* the same bad picture as before */
			lastBad[slot] = h;
			/* a picture that is neither: where does it differ from the nearer reference? */
			const Slot &ref = eA <= eB ? g_ref[0] : g_ref[1];
			long minx = 1 << 30, maxx = -1, miny = 1 << 30, maxy = -1;
			for (size_t j = 0; j < again.w.size(); j++)
				if (again.w[j] != ref.w[j])
				{
					long ry = (long)(j / SAMPLES_PER_ROW) * ROW_STEP, px = (long)(j % SAMPLES_PER_ROW) * WORD_STEP * 2;
					if (px < minx) minx = px;
					if (px > maxx) maxx = px;
					if (ry < miny) miny = ry;
					if (ry > maxy) maxy = ry;
				}
			if (r->anomalies < 30)
				printf("DISP ANOMALY in '%s' near frame %d (last sent scene %c): slot %d is neither A nor B, %zu of %zu samples differ from %c; pixels x %ld..%ld, y %ld..%ld of the slot\n", r->name, frame,
				       'A' + scene, slot, eA <= eB ? eA : eB, again.w.size(), eA <= eB ? 'A' : 'B', minx, maxx, miny, maxy);
			save_slot("x", slot, (int)r->anomalies);
			r->anomalies++;
		}
		double ms = (now_us() - t0) / 1000.0;
		r->scans++;
		r->scanMs += ms;
		if (ms > r->scanMax) r->scanMax = ms;
		usleep(1000);
	}
	return NULL;
}

static void run_pattern(const Pattern &p, bool withReads)
{
	if (abort_requested())
		return;
	int frames = getenv("DM_DISPTEST_QUICK") ? p.frames / 8 + 1 : p.frames;
	printf("DISP == %s: %d frames%s\n", p.name, frames, withReads ? ", with continuous read-back of all slots" : ", timing only");
	fflush(stdout);
	Stat present;
	Reader rd;
	rd.name = p.name;
	pthread_t th;
	bool started = false;
	if (withReads)
		started = pthread_create(&th, NULL, reader_main, &rd) == 0;
	uint32_t rnd = 12345;
	uint64_t t0 = now_us(), next = t0;
	int scene = 0, burst = 0;
	for (int i = 0; i < frames && !abort_requested(); i++)
	{
		switch (p.kind)
		{
		case 1: rnd = rnd * 1103515245u + 12345u; scene = ((rnd >> 16) & 3) == 0; break;
		case 2: scene = burst & 1; break;
		default: scene = i & 1; break;
		}
		draw_scene(scene);
		uint64_t ts = now_us();
		rd.lastScene = scene;
		rd.frameNo = i;
		present_frame();
		present.add((now_us() - ts) / 1000.0);
		switch (p.kind)
		{
		case 2:
			if (++burst % 6 == 0) next = now_us() + 150000; else next = now_us();
			break;
		case 3:
			rnd = rnd * 1103515245u + 12345u;
			next = ts + 5000 + (rnd >> 8) % 55000;
			break;
		case 4: next = now_us(); break;
		default: next = ts + p.paceUs; break;
		}
		wait_until(next);
	}
	usleep(300000);   /* the last frames get rendered and looked at */
	if (started)
	{
		rd.stop = true;
		pthread_join(th, NULL);
	}
	double secs = (now_us() - t0) / 1e6;
	printf("DISP   present() calls: %d, avg %.2f ms, min %.2f, max %.2f, over 5 ms: %d, over 10 ms: %d, over 20 ms: %d; %.1f frames/s\n", present.n, present.n ? present.sum / present.n : 0,
	       present.n ? present.mn : 0, present.mx, present.over5, present.over10, present.over20, secs > 0 ? present.n / secs : 0);
	if (started)
		printf("DISP   %ld scans of all slots (%.0f ms each, %.0f at most), %ld slot reads: %ld looked like A, %ld like B, %ld were being written (fine on a second look), %ld ANOMALOUS\n", rd.scans,
		       rd.scans ? rd.scanMs / rd.scans : 0, rd.scanMax, rd.slotReads, rd.asA, rd.asB, rd.transient, rd.anomalies);
	printf("DISP   %s RESULT: %s\n", p.name, started ? (rd.anomalies ? "ANOMALIES SEEN" : "all slots clean") : "timing only");
	fflush(stdout);
}

static void run_series(const char *label, bool withReads)
{
	printf("DISP ===== series: %s =====\n", label);
	fflush(stdout);
	if (withReads && !capture_refs())
		withReads = false;
	static const Pattern pats[] = {
		{ "alternate every 25 ms (game pace)", 400, 0, 25000 },
		{ "random A/B every 16.7 ms (60 Hz)", 500, 1, 16667 },
		{ "bursts of 6 frames then 150 ms rest", 120, 2, 0 },
		{ "jittered 5-60 ms", 300, 3, 0 },
		{ "slow, every 100 ms", 80, 0, 100000 },
		{ "free-running (no wait)", 300, 4, 0 },
	};
	for (size_t i = 0; i < sizeof pats / sizeof pats[0] && !abort_requested(); i++)
		run_pattern(pats[i], withReads);
}

int disptest_run()
{
	printf("DISP display test starting; Select+Start leaves\n");
	printf("DISP our canvas %d x %d\n", SW, SH);
	fb_probe();
	if (!load_assets())
	{
		printf("DISP could not load the Maze assets (data/maps/maze.xml/data/...)\n");
		fflush(stdout);
		return 1;
	}
	printf("DISP read-back is %s\n", g_fb.ok ? "possible (/dev/fb0 mapped)" : "NOT possible (only timing will be measured)");
	fflush(stdout);
	audio_init();
	bool music = getenv("DM_DISPTEST_NOMUSIC") == NULL;
	if (music)
	{
		music_play_folder("The Maze");
		for (int i = 0; i < 30; i++)   /* let the track load */
		{
			music_update();
			usleep(20000);
		}
	}
	run_series(music ? "music playing" : "no music", g_fb.ok);
	if (music && !g_abort)
	{
		music_play_folder("");   /* stops the music (no such folder) */
		music_set_volume(0);
		usleep(300000);
		run_series("no music", g_fb.ok);
	}
	printf("DISP display test finished\n");
	fflush(stdout);
	audio_shutdown();
	return 0;
}
