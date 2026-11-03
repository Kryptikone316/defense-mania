/* Display test for the SF3500 build of Defense Mania (started with DM_DISPTEST=1, see DefensemaniaDisp).
 *
 * Question it answers: does what the game hands to the display driver come out of the driver unchanged? A rare "ghost"
 * (a tower that vanishes for two frames and shows up elsewhere) was filmed on the device. The driver copies every frame
 * into its own queue (video_driver_disp_frame -> fbdev_draw_frame) and separate threads render it through the graphics
 * engine into /dev/fb0, so the artifact must happen after our call returns. This test
 *   1. looks at /dev/fb0 (geometry, whether it can be read and whether it follows what is sent),
 *   2. times every present call under several pacings,
 *   3. replays game-like frames (the Maze map, towers, an enemy, the HUD) as two scenes A and B that differ in a few
 *      small places, like consecutive game frames, in several rhythms (25 ms, 16.7 ms, random, bursts, free-running),
 *      and reads /dev/fb0 back at several delays after each present. Every read must look like A or B (or a mix of the
 *      two while the driver is still copying); anything that is neither is the artifact and is logged with its place.
 * It runs the whole series with music playing and once more without. Select + Start leaves at any time. */
#include "audio.hpp"
#include "gfx.hpp"
#include "hwdisp.hpp"
#include "joykey.hpp"

#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
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

/* ------------------------------------------------------------------ /dev/fb0 */
struct Fb
{
	int fd = -1;
	fb_var_screeninfo v;
	fb_fix_screeninfo f;
	const uint8_t *map = NULL;
	size_t len = 0;
	bool ok = false;
	int step = 7;       /* every step-th 32-bit word is sampled */
	int index = 0;      /* /dev/fb<index> */
};

static Fb g_fb;           /* the one being read back */
static Fb g_cand[4];      /* every frame buffer that could be mapped */

/* Self-check without a device (DM_DISPTEST_FAKEFB=1, for the emulator): a fake /dev/fb0 that is a copy of the last frame
 * sent, with a block of it displaced now and then, to prove that the read-back and the anomaly report work. */
static uint8_t g_fakeFb[640 * 480 * 2];
static bool g_fake;
static int g_fakeCount;

static void fake_after_present(const uint16_t *canvas)
{
	if (!g_fake)
		return;
	size_t line = (size_t)SW;
	memcpy(g_fakeFb, canvas, line * SH * 2);
	if (++g_fakeCount % 97 == 0)   /* the glitch: a 64 x 64 block copied to another place */
		for (int y = 0; y < 64; y++)
			memcpy(&g_fakeFb[((size_t)(60 + y) * line + 400) * 2], &g_fakeFb[((size_t)(60 + y) * line + 80) * 2], 64 * 2);
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
		g_fb.fd = -1;
		memset(&g_fb.v, 0, sizeof g_fb.v);
		memset(&g_fb.f, 0, sizeof g_fb.f);
		g_fb.v.xres = SW; g_fb.v.yres = SH; g_fb.v.yres_virtual = SH; g_fb.v.bits_per_pixel = 16;
		g_fb.f.line_length = SW * 2;
		g_fb.f.smem_len = sizeof g_fakeFb;
		g_fb.map = g_fakeFb;
		g_fb.len = sizeof g_fakeFb;
		g_fb.ok = true;
		g_cand[0] = g_fb;
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
			printf("DISP   res %ux%u virtual %ux%u offset %u,%u bpp %u rotate %u  line %u bytes  smem_len %u  type %u visual %u accel %u\n", v.xres, v.yres, v.xres_virtual, v.yres_virtual,
			       v.xoffset, v.yoffset, v.bits_per_pixel, v.rotate, f.line_length, f.smem_len, f.type, f.visual, f.accel);
		if (!r1 && !r2 && f.smem_len > 0)
		{
			size_t len = f.smem_len > (16u << 20) ? (16u << 20) : f.smem_len;
			void *m = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
			if (m == MAP_FAILED)
				printf("DISP   mmap of %s failed (%s)\n", path, strerror(errno));
			else
			{
				Fb &c = g_cand[i];
				c.fd = fd;
				c.v = v;
				c.f = f;
				c.map = (const uint8_t *)m;
				c.len = len;
				c.ok = true;
				c.index = i;
				c.step = len > (4u << 20) ? 61 : 7;   /* the big canvas is sampled more sparsely */
				printf("DISP   mapped %zu bytes of %s for reading (sampling every %d words)\n", len, path, c.step);
				continue;   /* keep fd open */
			}
		}
		close(fd);
	}
	fflush(stdout);
}

/* One sample set: every 7th 32-bit word of the visible page (the page the driver is showing right now). */
struct Samples
{
	std::vector<uint32_t> w;
	unsigned yoff = 0;
	uint64_t t = 0;
};

static void fb_read(Samples &s)
{
	/* the whole mapped frame (these frame buffers do not pan: the panel one is exactly one screen) */
	size_t page = (size_t)g_fb.f.line_length * (g_fb.v.yres_virtual ? g_fb.v.yres_virtual : g_fb.v.yres);
	if (page > g_fb.len || page == 0)
		page = g_fb.len;
	size_t words = page / 4;
	int step = g_fb.step;
	s.yoff = 0;
	s.w.resize((words + step - 1) / step);
	const uint32_t *p = (const uint32_t *)g_fb.map;
	size_t n = 0;
	for (size_t i = 0; i < words; i += step)
		s.w[n++] = p[i];
	s.t = now_us();
}

static size_t count_diff(const Samples &a, const Samples &b)
{
	size_t n = a.w.size() < b.w.size() ? a.w.size() : b.w.size(), d = 0;
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
static void draw_scene(int which, int phase = 0)
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
	(void)phase;
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

static Samples g_gold[2];
static bool g_haveGold;
static int g_dumps;

static void save_dump(const char *tag, const Samples &s, int idx)
{
	if (g_dumps >= 8)
		return;
	g_dumps++;
	char name[96];
	snprintf(name, sizeof name, "dbg/disp_anomaly_%d_%s.raw", idx, tag);
	FILE *f = fopen(name, "wb");
	if (!f)
		return;
	size_t page = (size_t)g_fb.f.line_length * (g_fb.v.yres_virtual ? g_fb.v.yres_virtual : g_fb.v.yres);
	size_t base = 0;
	if (page > g_fb.len || page == 0) page = g_fb.len;
	fwrite(g_fb.map + base, 1, page, f);
	fclose(f);
	printf("DISP   saved /dev/fb%d to %s (%u x %u, line %u bytes, %u bpp)\n", g_fb.index, name, g_fb.v.xres, g_fb.v.yres, g_fb.f.line_length, g_fb.v.bits_per_pixel);
}

/* present scene `which` and wait until the driver has shown it */
static void settle(int which)
{
	for (int i = 0; i < 3; i++)
	{
		draw_scene(which);
		present_frame();
		usleep(90000);
	}
}

static bool capture_golds()
{
	g_haveGold = false;
	static const int order[4] = { 1, 0, 2, 3 };   /* the 480x854 panel buffer first: it is what is shown */
	for (int k = 0; k < 4 && !g_haveGold && !g_abort; k++)
	{
		const Fb &c = g_cand[order[k]];
		if (!c.ok)
			continue;
		g_fb = c;
		printf("DISP trying /dev/fb%d for the read-back\n", c.index);
		bool stable = true;
		for (int w = 0; w < 2; w++)
		{
			settle(w);
			usleep(250000);
			Samples a, b;
			fb_read(a);
			usleep(60000);
			fb_read(b);
			size_t d = count_diff(a, b);
			printf("DISP   gold %c from /dev/fb%d: %zu samples, %zu differ between two reads 60 ms apart%s\n", 'A' + w, c.index, a.w.size(), d, d ? "  (the picture is not stable)" : "");
			stable = stable && d == 0;
			g_gold[w] = b;
		}
		size_t ab = count_diff(g_gold[0], g_gold[1]);
		printf("DISP   scene A and scene B differ in %zu of %zu samples of /dev/fb%d%s\n", ab, g_gold[0].w.size(), c.index,
		       ab == 0 ? "  -> it does not show the difference" : !stable ? "  -> but it is not stable, cannot be used" : "");
		g_haveGold = ab > 0 && stable;
		if (g_haveGold)
			printf("DISP read-back uses /dev/fb%d\n", c.index);
	}
	if (!g_haveGold)
		printf("DISP no frame buffer could be used for the read-back\n");
	fflush(stdout);
	return g_haveGold;
}

struct Pattern
{
	const char *name;
	int frames;
	int kind;   /* 0 alternate A,B at paceUs; 1 random A/A/A/B at paceUs; 2 bursts; 3 jitter; 4 free run */
	int paceUs;
};

/* The reader runs on a thread of its own and samples /dev/fb0 over and over while the main thread presents frames at the
 * pace of the game; each sample is filed by how long ago the last present was. */
static const int AGE_BUCKETS = 6;
static const int g_ageLimitsMs[AGE_BUCKETS] = { 4, 8, 16, 33, 66, 100000 };

struct Reader
{
	volatile bool stop = false;
	volatile uint64_t lastPresent = 0;
	volatile int lastScene = 0, prevScene = 0, frameNo = 0;
	long cnt[AGE_BUCKETS][4] = {};   /* [age][A, B, mix, ANOMALY] */
	long reads = 0, anomalies = 0;
	double readMs = 0, readMax = 0;
	const char *name = "";
};

static void *reader_main(void *arg)
{
	Reader *r = (Reader *)arg;
	while (!r->stop)
	{
		uint64_t t0 = now_us();
		int scene = r->lastScene, prev = r->prevScene, frame = r->frameNo;
		uint64_t lp = r->lastPresent;
		Samples s;
		fb_read(s);
		double ms = (now_us() - t0) / 1000.0;
		r->readMs += ms;
		if (ms > r->readMax) r->readMax = ms;
		size_t dA = count_diff(s, g_gold[0]), dB = count_diff(s, g_gold[1]);
		int cls;
		if (dA == 0) cls = 0;
		else if (dB == 0) cls = 1;
		else
		{
			size_t neither = 0, n = s.w.size();
			for (size_t j = 0; j < n; j++)
				neither += s.w[j] != g_gold[0].w[j] && s.w[j] != g_gold[1].w[j];
			cls = neither == 0 ? 2 : 3;
			if (cls == 3)
			{
				size_t line = g_fb.f.line_length / 4;
				long minx = 1 << 30, maxx = -1, miny = 1 << 30, maxy = -1;
				for (size_t j = 0; j < n; j++)
					if (s.w[j] != g_gold[0].w[j] && s.w[j] != g_gold[1].w[j])
					{
						size_t word = j * g_fb.step;
						long x = (long)(word % line), y = (long)(word / line);
						if (x < minx) minx = x;
						if (x > maxx) maxx = x;
						if (y < miny) miny = y;
						if (y > maxy) maxy = y;
					}
				if (r->anomalies < 40)
					printf("DISP ANOMALY in '%s' around frame %d (last sent scene %c, before it %c), %.1f ms after that present: %zu samples are neither A nor B; words x %ld..%ld, rows %ld..%ld (diff vs A %zu, vs B %zu)\n",
					       r->name, frame, 'A' + scene, 'A' + prev, (s.t - lp) / 1000.0, neither, minx, maxx, miny, maxy, dA, dB);
				save_dump("x", s, (int)r->anomalies);
				r->anomalies++;
			}
		}
		double age = lp ? (s.t - lp) / 1000.0 : 1e6;
		int b = 0;
		while (b < AGE_BUCKETS - 1 && age >= g_ageLimitsMs[b]) b++;
		r->cnt[b][cls]++;
		r->reads++;
		usleep(1500);
	}
	return NULL;
}

static void run_pattern(const Pattern &p, bool withReads)
{
	if (abort_requested())
		return;
	int frames = getenv("DM_DISPTEST_QUICK") ? p.frames / 8 + 1 : p.frames;
	printf("DISP == %s: %d frames%s\n", p.name, frames, withReads ? ", with continuous read-back" : ", timing only");
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
	int scene = 0, prevScene = 0, burst = 0;
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
		rd.prevScene = prevScene;
		rd.lastScene = scene;
		rd.frameNo = i;
		present_frame();
		uint64_t te = now_us();
		rd.lastPresent = te;
		present.add((te - ts) / 1000.0);
		prevScene = scene;
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
	usleep(120000);   /* the last frames get shown */
	if (started)
	{
		rd.stop = true;
		pthread_join(th, NULL);
	}
	double secs = (now_us() - t0) / 1e6;
	printf("DISP   present() calls: %d, avg %.2f ms, min %.2f, max %.2f, over 5 ms: %d, over 10 ms: %d, over 20 ms: %d; %.1f frames/s\n", present.n, present.n ? present.sum / present.n : 0,
	       present.n ? present.mn : 0, present.mx, present.over5, present.over10, present.over20, secs > 0 ? present.n / secs : 0);
	if (started)
	{
		printf("DISP   %ld read-backs (a read takes %.1f ms on average, %.1f at most); by time since the last present: shows A | shows B | mix of A and B | NEITHER\n", rd.reads, rd.reads ? rd.readMs / rd.reads : 0, rd.readMax);
		for (int b = 0; b < AGE_BUCKETS; b++)
			printf("DISP     age %s%3d ms: %6ld | %6ld | %6ld | %6ld\n", b == AGE_BUCKETS - 1 ? ">= " : "<  ", b == AGE_BUCKETS - 1 ? g_ageLimitsMs[b - 1] : g_ageLimitsMs[b], rd.cnt[b][0], rd.cnt[b][1], rd.cnt[b][2], rd.cnt[b][3]);
		printf("DISP   %s RESULT: %ld read-backs, %ld anomalous\n", p.name, rd.reads, rd.anomalies);
	}
	else
		printf("DISP   %s RESULT: timing only\n", p.name);
	fflush(stdout);
}

static void run_series(const char *label, bool withReads)
{
	printf("DISP ===== series: %s =====\n", label);
	fflush(stdout);
	if (withReads && !capture_golds())
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
	printf("DISP panel/canvas: our canvas %d x %d\n", SW, SH);
	fb_probe();
	if (!load_assets())
	{
		printf("DISP could not load the Maze assets (data/maps/maze.xml/data/...)\n");
		fflush(stdout);
		return 1;
	}
	bool anyFb = g_cand[0].ok || g_cand[1].ok || g_cand[2].ok || g_cand[3].ok;
	printf("DISP read-back is %s\n", anyFb ? "possible (frame buffers mapped)" : "NOT possible (only timing will be measured)");
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
	run_series(music ? "music playing" : "no music", anyFb);
	if (music && !g_abort)
	{
		music_play_folder("");   /* stops the music (no such folder) */
		music_set_volume(0);
		usleep(300000);
		run_series("no music", anyFb);
	}
	printf("DISP display test finished\n");
	fflush(stdout);
	audio_shutdown();
	return 0;
}
