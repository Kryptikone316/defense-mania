/* In-game check of what the display driver makes of our frames.
 *
 * What the display test found out about the driver (DefensemaniaDisp, section 22-25 of the handoff): every frame sent to
 * video_driver_disp_frame is copied into a queue and rendered by the graphics engine into a ring of 18 frame pairs in
 * /dev/fb0 (slot 2k = left, 640 x 240, the frame rotated a quarter turn clockwise and squeezed; slot 2k+1 = the same frame
 * through a second stage). The left slot is a plain rotate-and-scale of the canvas, so for any frame we sent we can
 * predict what it should look like. This module keeps a copy of the last 40 frames, and a low-priority thread looks at the
 * left slots about ten times a second: it finds which frame a slot should hold, predicts its picture (colour averages of
 * 32 x 24 pixel cells at a sparse set of sample points, so texture detail does not matter) and compares. A cell whose
 * error jumps far above its own usual level (a tower that is missing, or a copy of it somewhere else) is looked at again
 * a few milliseconds later and, if it is still wrong, reported in log.txt as SLOTCHECK ANOMALY with the slot and the
 * frame saved to files for a look. Nothing is touched when slot_check is off (the default). */
#include "slotcheck.hpp"
#include "devhooks.hpp"

#ifdef DM_TESTHOOKS   /* a diagnostic: left out of the release build, which gets the empty versions at the end of this file */
#include "gfx.hpp"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

const int SLOT_W = 640, SLOT_H = 240, PAIRS = 18;
const int ROW_STEP = 3, WORD_STEP = 7, WORDS_PER_ROW = 320;
const int SAMPLE_ROWS = SLOT_H / ROW_STEP;                           /* 80 */
const int SAMPLE_COLS = (WORDS_PER_ROW + WORD_STEP - 1) / WORD_STEP;   /* 46 */
const int SAMPLES = SAMPLE_ROWS * SAMPLE_COLS;
const int CELL_W = 32, CELL_H = 24, NCX = SLOT_W / CELL_W, NCY = SLOT_H / CELL_H, NCELLS = NCX * NCY;
const int RING = 40;
uint64_t g_minAge = 100000;
useconds_t g_rereadUs = 15000;   /* how long to wait before looking at a doubtful slot again */   /* a frame is looked for in its slot only after the driver had time to render it (the emulator's fake is slow) */

struct FrameRec
{
	uint16_t *px = NULL;
	uint32_t n = 0;
	uint64_t t = 0;
};

FrameRec g_fr[RING];
volatile uint32_t g_sent = 0;
int g_W = 0, g_H = 0;
bool g_on = false, g_fake = false;
uint8_t *g_fakeMap = NULL;
const uint8_t *g_map = NULL;
size_t g_len = 0, g_line = 2560;
pthread_t g_th;
bool g_threadOn = false;
volatile bool g_stop = false;
volatile int g_offPub = -1;   /* the calibrated ring offset, for the mark files */

uint64_t now_us()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
}

inline void px565(uint16_t v, float &r, float &g, float &b)
{
	r = ((v >> 11) & 31) * (255.0f / 31);
	g = ((v >> 5) & 63) * (255.0f / 63);
	b = (v & 31) * (255.0f / 31);
}

/* the driver's rotate-and-scale as measured on the device: a quarter turn clockwise, 640 x 358 -> 640 x 240, bilinear,
 * (measured exactly from the device: slot (xs, ys) reads canvas (ys * 640 / 240, 357 - xs * 358 / 640); the right slot of a pair is the same
 * picture half a slot pixel further along) */
void model_pixel(const uint16_t *c, float xs, float ys, float out[3], float ox = 0.0f)
{
	float xc = ys * g_W / (float)SLOT_H + ox;
	float yc = (g_H - 1) - xs * g_H / (float)SLOT_W;
	if (xc < 0) xc = 0;
	if (yc < 0) yc = 0;
	if (xc > g_W - 1) xc = (float)(g_W - 1);
	if (yc > g_H - 1) yc = (float)(g_H - 1);
	int x0 = (int)xc, y0 = (int)yc, x1 = x0 + 1 < g_W ? x0 + 1 : x0, y1 = y0 + 1 < g_H ? y0 + 1 : y0;
	float fx = xc - x0, fy = yc - y0;
	float r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
	px565(c[(size_t)y0 * g_W + x0], r00, g00, b00);
	px565(c[(size_t)y0 * g_W + x1], r10, g10, b10);
	px565(c[(size_t)y1 * g_W + x0], r01, g01, b01);
	px565(c[(size_t)y1 * g_W + x1], r11, g11, b11);
	float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
	out[0] = r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11;
	out[1] = g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11;
	out[2] = b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11;
}

/* reads the sample words of a left slot */
void read_slot(int slot, uint32_t *w)
{
	int x0 = (slot % 2) * SLOT_W, y0 = (slot / 2) * SLOT_H;
	size_t n = 0;
	for (int r = 0; r < SAMPLE_ROWS; r++)
	{
		const uint32_t *row = (const uint32_t *)(g_map + (size_t)(y0 + r * ROW_STEP) * g_line + (size_t)x0 * 2);
		for (int k = 0; k < SAMPLE_COLS; k++)
			w[n++] = row[k * WORD_STEP];
	}
}

/* The model is evaluated at the same 7360 sample points every time, so where each one reads the canvas (four pixels and
 * their weights) is worked out once. Evaluating a frame is then four table reads per sample, in integers. */
struct Tap
{
	uint32_t i00, i10, i01, i11;   /* canvas pixel indexes */
	uint16_t w00, w10, w01, w11;   /* weights, summing to 256 */
	uint16_t cell;
};
std::vector<Tap> g_taps, g_tapsR;   /* sample order: row, word, pixel of the word (0 then 1); left slot, right slot */
const Tap *g_tp = NULL;            /* the set in use */
int g_cellCount[NCELLS];
uint8_t g_lut5[32], g_lut6[64];

void build_set(std::vector<Tap> &taps, float ox)
{
	taps.resize((size_t)SAMPLES * 2);
	memset(g_cellCount, 0, sizeof g_cellCount);
	size_t t = 0;
	for (int r = 0; r < SAMPLE_ROWS; r++)
		for (int k = 0; k < SAMPLE_COLS; k++)
			for (int sub = 0; sub < 2; sub++, t++)
			{
				int xs = k * WORD_STEP * 2 + sub, ys = r * ROW_STEP;
				float xc = ys * g_W / (float)SLOT_H + ox;
				float yc = (g_H - 1) - xs * g_H / (float)SLOT_W;
				if (xc < 0) xc = 0;
				if (yc < 0) yc = 0;
				if (xc > g_W - 1) xc = (float)(g_W - 1);
				if (yc > g_H - 1) yc = (float)(g_H - 1);
				int x0 = (int)xc, y0 = (int)yc, x1 = x0 + 1 < g_W ? x0 + 1 : x0, y1 = y0 + 1 < g_H ? y0 + 1 : y0;
				float fx = xc - x0, fy = yc - y0;
				Tap &p = taps[t];
				p.i00 = (uint32_t)(y0 * g_W + x0);
				p.i10 = (uint32_t)(y0 * g_W + x1);
				p.i01 = (uint32_t)(y1 * g_W + x0);
				p.i11 = (uint32_t)(y1 * g_W + x1);
				int w00 = (int)((1 - fx) * (1 - fy) * 256 + 0.5f), w10 = (int)(fx * (1 - fy) * 256 + 0.5f), w01 = (int)((1 - fx) * fy * 256 + 0.5f);
				int w11 = 256 - w00 - w10 - w01;
				p.w00 = (uint16_t)w00; p.w10 = (uint16_t)w10; p.w01 = (uint16_t)w01; p.w11 = (uint16_t)(w11 < 0 ? 0 : w11);
				int cx = xs / CELL_W, cy = ys / CELL_H;
				if (cx >= NCX) cx = NCX - 1;
				if (cy >= NCY) cy = NCY - 1;
				p.cell = (uint16_t)(cy * NCX + cx);
				g_cellCount[p.cell]++;
			}
}

void build_tables()
{
	for (int i = 0; i < 32; i++) g_lut5[i] = (uint8_t)(i * 255 / 31);
	for (int i = 0; i < 64; i++) g_lut6[i] = (uint8_t)(i * 255 / 63);
	build_set(g_taps, 0.0f);
	build_set(g_tapsR, 1.5f);   /* the right slot of a pair: the same picture half a slot pixel further along */
	g_tp = g_taps.data();
}

/* cell errors of a slot's samples against the model of a frame; returns the mean cell error */
float eval_slot(const uint32_t *w, const uint16_t *canvas, float cellErr[NCELLS])
{
	int sumS[NCELLS][3], sumM[NCELLS][3];
	memset(sumS, 0, sizeof sumS);
	memset(sumM, 0, sizeof sumM);
	const Tap *tp = g_tp;
	for (int i = 0; i < SAMPLES; i++)
	{
		uint32_t word = w[i];
		for (int sub = 0; sub < 2; sub++, tp++)
		{
			uint32_t v = (word >> (16 * sub)) & 0xFFFF;
			int c = tp->cell;
			sumS[c][0] += g_lut5[v >> 11]; sumS[c][1] += g_lut6[(v >> 5) & 63]; sumS[c][2] += g_lut5[v & 31];
			uint32_t a = canvas[tp->i00], b = canvas[tp->i10], cc = canvas[tp->i01], d = canvas[tp->i11];
			int r = (g_lut5[a >> 11] * tp->w00 + g_lut5[b >> 11] * tp->w10 + g_lut5[cc >> 11] * tp->w01 + g_lut5[d >> 11] * tp->w11) >> 8;
			int g = (g_lut6[(a >> 5) & 63] * tp->w00 + g_lut6[(b >> 5) & 63] * tp->w10 + g_lut6[(cc >> 5) & 63] * tp->w01 + g_lut6[(d >> 5) & 63] * tp->w11) >> 8;
			int bl = (g_lut5[a & 31] * tp->w00 + g_lut5[b & 31] * tp->w10 + g_lut5[cc & 31] * tp->w01 + g_lut5[d & 31] * tp->w11) >> 8;
			sumM[c][0] += r; sumM[c][1] += g; sumM[c][2] += bl;
		}
	}
	float total = 0;
	for (int c = 0; c < NCELLS; c++)
	{
		if (!g_cellCount[c]) { cellErr[c] = 0; continue; }
		cellErr[c] = (float)(abs(sumS[c][0] - sumM[c][0]) + abs(sumS[c][1] - sumM[c][1]) + abs(sumS[c][2] - sumM[c][2])) / (3.0f * g_cellCount[c]);
		total += cellErr[c];
	}
	return total / NCELLS;
}

const FrameRec *frame_at(uint32_t n)
{
	if (g_sent == 0 || n >= g_sent || g_sent - n >= (uint32_t)RING - 2)
		return NULL;
	const FrameRec &f = g_fr[n % RING];
	return f.n == n ? &f : NULL;
}

int g_dumps = 0;
uint64_t g_t0 = 0;   /* when the check started */
double since_start() { return (now_us() - g_t0) / 1e6; }
long g_cls[3] = {0, 0, 0};          /* anomalies whose best match in the whole ring is: the chosen frame, another frame, no frame */
long g_deltaHist[9] = {};           /* other frame minus chosen frame, -4..+4 (index 0..8) */
long g_deltaOut = 0;
long g_known = 0, g_rightChecks = 0, g_rightAnom = 0;                   /* unexplained anomalies that only repeat a mismatch already seen in the same cells */
int g_noCount[NCELLS];
float g_noErr[NCELLS];                /* ... or farther away */

void save_dump(int slot, uint32_t frame, const uint32_t *words)
{
	(void)words;
	const FrameRec *f = frame_at(frame);
	if (!f)
		return;
	char name[96];
	snprintf(name, sizeof name, "slotcheck_%d_slot.raw", g_dumps % 10);
	FILE *o = fopen(name, "wb");
	if (o)
	{
		int x0 = (slot % 2) * SLOT_W, y0 = (slot / 2) * SLOT_H;
		for (int r = 0; r < SLOT_H; r++)
			fwrite(g_map + (size_t)(y0 + r) * g_line + (size_t)x0 * 2, 1, (size_t)SLOT_W * 2, o);
		fclose(o);
	}
	snprintf(name, sizeof name, "slotcheck_%d_canvas.raw", g_dumps % 10);
	o = fopen(name, "wb");
	if (o)
	{
		fwrite(f->px, 1, (size_t)g_W * g_H * 2, o);
		fclose(o);
	}
	printf("SLOTCHECK   saved slotcheck_%d_slot.raw (640 x 240, 16 bit, the driver's picture) and slotcheck_%d_canvas.raw (%d x %d, what we sent) at t=%.1f s; the newest 10 are kept, this is number %d\n", g_dumps % 10, g_dumps % 10, g_W, g_H, since_start(), g_dumps + 1);
	g_dumps++;
}

struct Stats
{
	long checks = 0, unmatched = 0, anomalies = 0, rereads = 0, pending = 0;
	long hist[51] = {};   /* cell errors in steps of 2 */
};

int calibrate(float cellErr[NCELLS])
{
	uint32_t n = g_sent;
	uint64_t now = now_us();
	float best = 1e30f, second = 1e30f;
	int bestOff = 0;
	std::vector<uint32_t> w(SAMPLES);
	for (int off = 0; off < PAIRS; off++)
	{
		float sum = 0;
		int used = 0;
		for (int back = 4; back < 30 && used < 4; back += 5)
		{
			if (n < (uint32_t)back + 1) break;
			const FrameRec *f = frame_at(n - 1 - back);
			if (!f || f->t + g_minAge > now) continue;
			read_slot(((f->n + off) % PAIRS) * 2, w.data());
			sum += eval_slot(w.data(), f->px, cellErr);
			used++;
		}
		if (!used) return -1;
		sum /= used;
		if (sum < best) { second = best; best = sum; bestOff = off; }
		else if (sum < second) second = sum;
	}
	printf("SLOTCHECK ring offset %d (error %.1f, next best %.1f)\n", bestOff, best, second);
	fflush(stdout);
	return bestOff;
}


/* a quick measure of how well a frame explains a slot: every 24th sample, plain differences */
float coarse_err(const uint32_t *w, const uint16_t *canvas)
{
	long sum = 0, n = 0;
	for (int i = 0; i < SAMPLES; i += 8)
	{
		uint32_t word = w[i];
		for (int sub = 0; sub < 2; sub++)
		{
			const Tap *tp = &g_tp[(size_t)i * 2 + sub];
			uint32_t v = (word >> (16 * sub)) & 0xFFFF;
			uint32_t a = canvas[tp->i00], b = canvas[tp->i10], cc = canvas[tp->i01], d = canvas[tp->i11];
			int r = (g_lut5[a >> 11] * tp->w00 + g_lut5[b >> 11] * tp->w10 + g_lut5[cc >> 11] * tp->w01 + g_lut5[d >> 11] * tp->w11) >> 8;
			int g = (g_lut6[(a >> 5) & 63] * tp->w00 + g_lut6[(b >> 5) & 63] * tp->w10 + g_lut6[(cc >> 5) & 63] * tp->w01 + g_lut6[(d >> 5) & 63] * tp->w11) >> 8;
			int bl = (g_lut5[a & 31] * tp->w00 + g_lut5[b & 31] * tp->w10 + g_lut5[cc & 31] * tp->w01 + g_lut5[d & 31] * tp->w11) >> 8;
			sum += abs(r - (int)g_lut5[v >> 11]) + abs(g - (int)g_lut6[(v >> 5) & 63]) + abs(bl - (int)g_lut5[v & 31]);
			n++;
		}
	}
	return n ? (float)sum / (3.0f * n) : 1e30f;
}

/* the frames (of the ones kept) that a slot's samples fit best, best first */
const int NPICK = 10;
int pick_frames(const uint32_t *w, uint32_t out[NPICK])
{
	uint32_t newest = g_sent;
	float sc[NPICK];
	int n = 0;
	for (uint32_t k = 1; k < (uint32_t)RING - 3 && k <= newest; k++)
	{
		uint32_t cand = newest - k;
		const FrameRec *fr = frame_at(cand);
		if (!fr) continue;
		float c = coarse_err(w, fr->px);
		int pos = n < NPICK ? n : NPICK;
		while (pos > 0 && sc[pos - 1] > c) pos--;
		if (pos >= NPICK) continue;
		int last = n < NPICK ? n : NPICK - 1;
		for (int j = last; j > pos; j--) { sc[j] = sc[j - 1]; out[j] = out[j - 1]; }
		sc[pos] = c;
		out[pos] = cand;
		if (n < NPICK) n++;
	}
	return n;
}

/* the frame a slot shows: of the few that fit best at a glance, the first one no cell is far off from, else the best one */
uint32_t pick_frame(const uint32_t *w, bool &found, float *errOut, const float *base, bool haveBase, uint32_t hint = 0xFFFFFFFFu)
{
	uint32_t cand[NPICK + 3];
	int n = 0;
	if (hint != 0xFFFFFFFFu)   /* the slot before held frame h: this one most likely holds h + 1 (or the same, or h + 2) */
		for (uint32_t d = 1; d <= 3; d++)
			if (frame_at(hint + (d == 3 ? 0 : d))) cand[n++] = hint + (d == 3 ? 0 : d);
	{
		uint32_t more[NPICK];
		int m = pick_frames(w, more);
		for (int i = 0; i < m; i++)
		{
			bool dup = false;
			for (int j = 0; j < n; j++) if (cand[j] == more[i]) dup = true;
			if (!dup) cand[n++] = more[i];
		}
	}
	found = n > 0;
	if (!n) return 0;
	float e[NCELLS];
	uint32_t best = cand[0];
	for (int i = 0; i < n; i++)
	{
		const FrameRec *fr = frame_at(cand[i]);
		if (!fr) continue;
		eval_slot(w, fr->px, e);
		int bad = 0;
		for (int c = 0; c < NCELLS; c++)
			if (e[c] > (haveBase ? base[c] : 0.0f) + 5 && e[c] > 7) bad++;
		if (i == 0 || bad == 0) { best = cand[i]; memcpy(errOut, e, sizeof(float) * NCELLS); }
		if (bad == 0) break;
	}
	return best;
}

void *reader_main(void *)
{
	setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 5);
	std::vector<uint32_t> w(SAMPLES), w2(SAMPLES), wR(SAMPLES), wR2(SAMPLES);
	float errA[NCELLS], errB[NCELLS], err2[NCELLS];
	float base[NCELLS], baseR[NCELLS];
	bool haveBase = false, haveBaseR = false;
	int lastWorstR[PAIRS];
	double lastTimeR[PAIRS];
	for (int i = 0; i < PAIRS; i++) { lastWorstR[i] = -1; lastTimeR[i] = -100; }
	int off = -1;
	uint64_t lastCal = 0, lastStatus = now_us();
	uint32_t prevF = 0xFFFFFFFFu;
	uint32_t lastRep[PAIRS];
	memset(lastRep, 0xFF, sizeof lastRep);
	Stats st;
	int unmatchedRun = 0;
	double calMs = 0, readMs = 0, evalMs = 0;
	long calibrations = 0, reads = 0, evals = 0, scans = 0;
	while (!g_stop)
	{
		uint32_t n = g_sent;
		uint64_t now = now_us();
		if (n < 60)
		{
			usleep(100000);
			continue;
		}
		if (false && !g_fake && (off < 0 || now - lastCal > 30000000ull || unmatchedRun > 20))
		{
			uint64_t c0 = now_us();
			int o = calibrate(errA);
			if (o >= 0) { off = o; g_offPub = o; }
			lastCal = now_us();
			calMs += (lastCal - c0) / 1000.0;
			calibrations++;
			unmatchedRun = 0;
			if (off < 0) { usleep(200000); continue; }
		}
		if (off < 0)
			off = 0;
		prevF = 0xFFFFFFFFu;
		for (int p = 0; p < PAIRS && !g_stop; p++)
		{
			n = g_sent;   /* a scan takes a while: frames keep coming, and slots get rewritten under it */
			now = now_us();
			/* which frame is in this slot? Nothing is assumed about the driver's order: a quick comparison (every 24th sample)
			 * with every frame we kept picks it, then that frame is checked in full */
			uint64_t tr0 = now_us();
			read_slot(p * 2, w.data());
			readMs += (now_us() - tr0) / 1000.0;
			reads++;
			uint64_t te0 = now_us();
			bool found = false;
			float errP[NCELLS];
			uint32_t f = pick_frame(w.data(), found, errP, base, haveBase, prevF);
			if (!found)
				continue;
			const FrameRec *fa = frame_at(f);
			if (!fa)
				continue;
			memcpy(errA, errP, sizeof errA);
			float ma = 0;
			for (int c = 0; c < NCELLS; c++) ma += errA[c];
			ma /= NCELLS;
			evals++;
			uint32_t chosen = f;
			prevF = f;
			float *err = errA;
			float mean = ma;
			evalMs += (now_us() - te0) / 1000.0;
			st.checks++;
			if (mean > 8)
			{
				st.unmatched++;
				unmatchedRun++;
				continue;
			}
			unmatchedRun = 0;
			for (int c = 0; c < NCELLS; c++)
			{
				int hb = (int)(err[c] / 2);
				st.hist[hb > 50 ? 50 : hb]++;
			}
			if (!haveBase)
			{
				memcpy(base, err, sizeof base);
				haveBase = true;
				continue;
			}
			int flagged = 0;
			for (int c = 0; c < NCELLS; c++)
				if (err[c] > base[c] + 5 && err[c] > 7)
					flagged++;
			if (flagged)
			{
				/* a write in progress, or real? look again */
				usleep(g_rereadUs);
				read_slot(p * 2, w2.data());
				st.rereads++;
				/* the slot may have been rewritten in the meantime: pick the matching frame again */
				uint32_t chosen2 = chosen;
				const FrameRec *fc = NULL;
				{
					bool ok2 = false;
					uint32_t c2 = pick_frame(w2.data(), ok2, err2, base, haveBase);
					if (ok2 && frame_at(c2))
					{
						chosen2 = c2;
						fc = frame_at(c2);
					}
				}
				chosen = chosen2;
				if (fc)
				{
					int still = 0, worst = -1;
					for (int c = 0; c < NCELLS; c++)
						if (err2[c] > base[c] + 5 && err2[c] > 7)
						{
							still++;
							if (worst < 0 || err2[c] - base[c] > err2[worst] - base[worst]) worst = c;
						}
					if (still && lastRep[p] != chosen)
					{
						lastRep[p] = chosen;
						st.anomalies++;
						printf("SLOTCHECK ANOMALY at t=%.1f s: frame %u (sent %.0f ms ago) in slot %d: %d of %d cells are far off; worst cell x %d..%d y %d..%d of the slot: error %.0f, usually %.0f\n", since_start(), chosen,
						       (now_us() - fc->t) / 1000.0, p * 2, still, NCELLS, (worst % NCX) * CELL_W, (worst % NCX) * CELL_W + CELL_W - 1, (worst / NCX) * CELL_H, (worst / NCX) * CELL_H + CELL_H - 1, err2[worst], base[worst]);
						for (int c = 0, shown = 0; c < NCELLS && shown < 4; c++)
							if (err2[c] > base[c] + 5 && err2[c] > 7 && c != worst)
							{
								printf("SLOTCHECK   also cell x %d..%d y %d..%d: error %.0f, usually %.0f\n", (c % NCX) * CELL_W, (c % NCX) * CELL_W + CELL_W - 1, (c / NCX) * CELL_H, (c / NCX) * CELL_H + CELL_H - 1, err2[c], base[c]);
								shown++;
							}
						/* is the picture any frame we sent? A frame explains it when no cell is far off; of those, the nearest to
						 * the expected frame counts (a still picture is the same in many frames). Otherwise the frame with the
						 * fewest cells off, then the smallest mean error, is the best guess. */
						{
							float errT[NCELLS];
							uint32_t bestF = chosen;
							int bestStill = 1 << 30;
							float bestM = 1e30f, chM = 1e30f;
							long bestDist = 1L << 30;
							bool bestOk = false;
							uint32_t newest = g_sent;
							for (uint32_t k = 1; k < (uint32_t)RING - 3 && k <= newest; k++)
							{
								uint32_t g = newest - k;
								const FrameRec *gf = frame_at(g);
								if (!gf) continue;
								float m = eval_slot(w2.data(), gf->px, errT);
								int st2 = 0;
								for (int c = 0; c < NCELLS; c++)
									if (errT[c] > base[c] + 5 && errT[c] > 7) st2++;
								if (g == chosen) chM = m;
								long dist = (long)g - (long)chosen;
								if (dist < 0) dist = -dist;
								bool ok = st2 == 0;
								bool better;
								if (ok != bestOk) better = ok;
								else if (ok) better = dist < bestDist || (dist == bestDist && m < bestM);
								else better = st2 < bestStill || (st2 == bestStill && m < bestM);
								if (better)
								{
									bestF = g; bestM = m; bestStill = st2; bestDist = dist; bestOk = ok;
								}
							}
							const FrameRec *bf = frame_at(bestF);
							int cls = bestF == chosen && bestOk ? 0 : bestOk ? 1 : 2;
							g_cls[cls]++;
							if (cls == 1)
							{
								long d = (long)bestF - (long)chosen;
								if (d >= -4 && d <= 4) g_deltaHist[d + 4]++; else g_deltaOut++;
							}
							printf("SLOTCHECK   whole ring: best match frame %u (%+ld from the expected one, sent %.0f ms ago), mean error %.1f against %.1f for the expected frame, %d cells far off -> %s\n", bestF, (long)bestF - (long)chosen,
							       bf ? (now_us() - bf->t) / 1000.0 : -1.0, bestM, chM, bestOk ? 0 : bestStill,
							       cls == 0 ? "the expected frame fits after all (a false alarm)" : cls == 1 ? "another frame we sent (frame timing, not a bad picture)" : "NO frame we sent matches this picture");
							if (cls == 2)
							{
								/* the same cells, the same size of error, again and again: a still picture the model gets slightly wrong
								 * (sharp text, small sprites), not a glitch. Only something new is dumped. */
								bool novel = false;
								for (int c = 0; c < NCELLS; c++)
									if (err2[c] > base[c] + 5 && err2[c] > 7)
									{
										if (g_noCount[c] < 3 || err2[c] > g_noErr[c] + 6) novel = true;
										g_noCount[c]++;
										g_noErr[c] = g_noCount[c] == 1 ? err2[c] : g_noErr[c] * 0.7f + err2[c] * 0.3f;
									}
								if (novel)
									save_dump(p * 2, bestF, w2.data());
								else
								{
									g_known++;
									printf("SLOTCHECK   (a mismatch seen before in the same cells: not saved)\n");
								}
							}
						}
						fflush(stdout);
					}
					continue;   /* a flagged slot does not teach the baseline */
				}
			}
			for (int c = 0; c < NCELLS; c++)
				base[c] = base[c] * 0.97f + err[c] * 0.03f;
			/* the right slot of the pair: the same picture, half a slot pixel further along; same frame unless it fits another better */
			{
				read_slot(p * 2 + 1, wR.data());
				g_tp = g_tapsR.data();
				float errR[NCELLS];
				uint32_t cf = chosen;
				const FrameRec *rf = frame_at(cf);
				int bad = 0;
				if (rf)
				{
					eval_slot(wR.data(), rf->px, errR);
					g_rightChecks++;
					if (haveBaseR)
						for (int c = 0; c < NCELLS; c++)
							if (errR[c] > baseR[c] + 5 && errR[c] > 7) bad++;
				}
				if (rf && !haveBaseR)
				{
					memcpy(baseR, errR, sizeof baseR);
					haveBaseR = true;
				}
				else if (bad)
				{
					usleep(g_rereadUs);
					read_slot(p * 2 + 1, wR2.data());
					bool okR = false;
					uint32_t c2 = pick_frame(wR2.data(), okR, errR, baseR, haveBaseR);
					bad = 0;
					int worstR = -1;
					if (okR)
						for (int c = 0; c < NCELLS; c++)
							if (errR[c] > baseR[c] + 5 && errR[c] > 7)
							{
								bad++;
								if (worstR < 0 || errR[c] - baseR[c] > errR[worstR] - baseR[worstR]) worstR = c;
							}
					if (bad && !(lastWorstR[p] == worstR && since_start() - lastTimeR[p] < 20.0))
					{
						lastWorstR[p] = worstR;
						lastTimeR[p] = since_start();
						g_rightAnom++;
						printf("SLOTCHECK RIGHT ANOMALY at t=%.1f s: frame %u in slot %d (right of pair %d): %d of %d cells are far off; worst cell x %d..%d y %d..%d of the slot: error %.0f, usually %.0f\n", since_start(), c2, p * 2 + 1, p, bad, NCELLS,
						       (worstR % NCX) * CELL_W, (worstR % NCX) * CELL_W + CELL_W - 1, (worstR / NCX) * CELL_H, (worstR / NCX) * CELL_H + CELL_H - 1, errR[worstR], baseR[worstR]);
						if (g_rightAnom <= 10)
							save_dump(p * 2 + 1, c2, wR2.data());
						fflush(stdout);
					}
				}
				if (!bad)
					for (int c = 0; c < NCELLS; c++)
						baseR[c] = baseR[c] * 0.97f + errR[c] * 0.03f;
				g_tp = g_taps.data();
			}
		}
		scans++;
		if (now_us() - lastStatus > 10000000ull)
		{
			lastStatus = now_us();
			long total = 0, acc = 0;
			for (int i = 0; i <= 50; i++) total += st.hist[i];
			int p50 = 0, p95 = 0, p99 = 0;
			for (int i = 0; i <= 50; i++)
			{
				acc += st.hist[i];
				if (!p50 && acc * 100 >= total * 50) p50 = i * 2;
				if (!p95 && acc * 100 >= total * 95) p95 = i * 2;
				if (!p99 && acc * 100 >= total * 99) p99 = i * 2;
			}
			printf("SLOTCHECK status: %u frames sent, %ld slot checks, %ld not rendered yet, %ld unmatched, %ld re-reads, %ld anomalies; cell error p50 %d, p95 %d, p99 %d; %ld scans, %ld calibrations (%.0f ms each), a slot read takes %.1f ms and its checks %.1f ms; anomalies by whole-ring search: %ld other-frame (offsets -4..+4: %ld %ld %ld %ld . %ld %ld %ld %ld, farther %ld), %ld NO frame matches (%ld of them repeats of known still-picture mismatches), %ld false alarms; right slots: %ld checks, %ld anomalies\n", (unsigned)g_sent, st.checks, st.pending, st.unmatched, st.rereads, st.anomalies, p50, p95, p99, scans, calibrations, calibrations ? calMs / calibrations : 0.0, reads ? readMs / reads : 0.0, st.checks ? evalMs / st.checks : 0.0, g_cls[1], g_deltaHist[0], g_deltaHist[1], g_deltaHist[2], g_deltaHist[3], g_deltaHist[5], g_deltaHist[6], g_deltaHist[7], g_deltaHist[8], g_deltaOut, g_cls[2], g_known, g_cls[0], g_rightChecks, g_rightAnom);
			fflush(stdout);
		}
		usleep(40000);
	}
	return NULL;
}

}   // namespace

bool slotcheck_start()
{
	if (cfg_num("slot_check", 0) == 0)
		return false;
	if (cfg_num("present_480", 0) != 0)
	{
		printf("SLOTCHECK is off: present_480 changes the frame the model expects\n");
		return false;
	}
	if (dm_env("DM_SLOTCHECK_FAKE"))
	{
		g_fake = true;
		g_minAge = 2500000;
		g_rereadUs = 120000;
		g_fakeMap = (uint8_t *)calloc(1, g_line * 4320);
		g_map = g_fakeMap;
		g_len = g_line * 4320;
		printf("SLOTCHECK using a FAKE frame buffer (self-check)\n");
	}
	else if (dm_env("DM_SLOTCHECK_FILE"))
	{
		int fd = open(dm_env("DM_SLOTCHECK_FILE"), O_RDONLY);
		struct stat sb;
		if (fd < 0 || fstat(fd, &sb) != 0)
			return false;
		void *m = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
		if (m == MAP_FAILED)
			return false;
		g_map = (const uint8_t *)m;
		g_len = (size_t)sb.st_size;
		printf("SLOTCHECK reading the saved frame buffer %s\n", dm_env("DM_SLOTCHECK_FILE"));
	}
	else
	{
		int fd = open("/dev/fb0", O_RDWR);
		if (fd < 0)
			fd = open("/dev/fb0", O_RDONLY);
		fb_var_screeninfo v;
		fb_fix_screeninfo f;
		if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f) || v.bits_per_pixel != 16 || f.smem_len < f.line_length * 4320u)
		{
			printf("SLOTCHECK is off: /dev/fb0 cannot be used (%s)\n", fd < 0 ? strerror(errno) : "not the layout the check expects");
			if (fd >= 0) close(fd);
			return false;
		}
		size_t len = f.smem_len > (16u << 20) ? (16u << 20) : f.smem_len;
		void *m = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
		if (m == MAP_FAILED)
		{
			printf("SLOTCHECK is off: cannot map /dev/fb0 (%s)\n", strerror(errno));
			close(fd);
			return false;
		}
		g_map = (const uint8_t *)m;
		g_len = len;
		g_line = f.line_length;
	}
	g_W = SW;
	g_H = SH;
	build_tables();
	for (int i = 0; i < RING; i++)
	{
		g_fr[i].px = (uint16_t *)malloc((size_t)g_W * g_H * 2);
		if (!g_fr[i].px)
		{
			printf("SLOTCHECK is off: no memory for the frame copies\n");
			return false;
		}
	}
	g_on = true;
	g_stop = false;
	g_t0 = now_us();
	g_threadOn = pthread_create(&g_th, NULL, reader_main, NULL) == 0;
	printf("SLOTCHECK started: the last %d frames are kept and the driver's left slots are checked about ten times a second\n", RING);
	fflush(stdout);
	return g_threadOn;
}

void slotcheck_frame(const uint16_t *canvas, int w, int h)
{
	if (!g_on || w != g_W || h != g_H)
		return;
	uint32_t n = g_sent;
	FrameRec &r = g_fr[n % RING];
	memcpy(r.px, canvas, (size_t)w * h * 2);
	r.n = n;
	r.t = now_us();
	__sync_synchronize();
	g_sent = n + 1;
	static long markAt = dm_env("DM_SLOTCHECK_MARK_AT") ? atol(dm_env("DM_SLOTCHECK_MARK_AT")) : -1;   /* test hook */
	if (markAt >= 0 && (long)g_sent == markAt)
		slotcheck_mark();
}

void slotcheck_after_present(const uint16_t *canvas, int w, int h)
{
	if (!g_on || !g_fake || w != g_W || h != g_H)
		return;
	/* the emulator's stand-in for the driver: renders the model into the ring, with a displaced block now and then */
	static uint32_t count = 0;
	static std::vector<uint16_t> tmp((size_t)SLOT_W * SLOT_H), tmpR((size_t)SLOT_W * SLOT_H);
	int pair = count % PAIRS;
	uint8_t *base = g_fakeMap;
	for (int y = 0; y < SLOT_H; y++)
		for (int x = 0; x < SLOT_W; x++)
		{
			float m[3], mr[3];
			model_pixel(canvas, (float)x, (float)y, m);   /* exactly the model: the fake driver is perfect */
			model_pixel(canvas, (float)x, (float)y, mr, 1.5f);
			tmp[(size_t)y * SLOT_W + x] = (uint16_t)((((int)(m[0] / 8)) << 11) | (((int)(m[1] / 4)) << 5) | ((int)(m[2] / 8)));
			tmpR[(size_t)y * SLOT_W + x] = (uint16_t)((((int)(mr[0] / 8)) << 11) | (((int)(mr[1] / 4)) << 5) | ((int)(mr[2] / 8)));
		}
	++count;
	if (count % 150 == 0)   /* the glitch: a tower-sized block copied somewhere else (in the picture, before it is "written") */
		for (int y = 0; y < 40; y++)
			memcpy(&tmp[(size_t)(100 + y) * SLOT_W + 420], &tmp[(size_t)(20 + y) * SLOT_W + 60], 100 * 2);
	if (count % 150 == 75)   /* the same, in a right slot only */
		for (int y = 0; y < 40; y++)
			memcpy(&tmpR[(size_t)(100 + y) * SLOT_W + 420], &tmpR[(size_t)(20 + y) * SLOT_W + 60], 100 * 2);
	for (int y = 0; y < SLOT_H; y++)   /* written in one go, as the real engine does */
	{
		memcpy(base + (size_t)(pair * SLOT_H + y) * g_line, &tmp[(size_t)y * SLOT_W], (size_t)SLOT_W * 2);
		memcpy(base + (size_t)(pair * SLOT_H + y) * g_line + (size_t)SLOT_W * 2, &tmpR[(size_t)y * SLOT_W], (size_t)SLOT_W * 2);
	}
}


namespace {

const int MARK_FRAMES = 24, MARK_MAX = 3;
int g_marks = 0;
volatile int g_markBusy = 0;

struct MarkJob
{
	int idx;
	uint8_t *ring;
	size_t ringLen;
	uint16_t *frames;
	int count;
	uint32_t num[MARK_FRAMES];
	double ageMs[MARK_FRAMES];
	uint8_t *panel;
	size_t panelLen;
	double t;
	uint32_t sent;
	int off;
};

void write_file(const char *name, const void *data, size_t len)
{
	FILE *o = fopen(name, "wb");
	if (!o)
		return;
	fwrite(data, 1, len, o);
	fclose(o);
}

void *mark_writer(void *arg)
{
	MarkJob *j = (MarkJob *)arg;
	char name[96];
	snprintf(name, sizeof name, "mark_%d_ring.raw", j->idx);
	write_file(name, j->ring, j->ringLen);
	snprintf(name, sizeof name, "mark_%d_frames.raw", j->idx);
	write_file(name, j->frames, (size_t)j->count * g_W * g_H * 2);
	if (j->panel)
	{
		snprintf(name, sizeof name, "mark_%d_panel.raw", j->idx);
		write_file(name, j->panel, j->panelLen);
	}
	snprintf(name, sizeof name, "mark_%d_info.txt", j->idx);
	FILE *o = fopen(name, "w");
	if (o)
	{
		fprintf(o, "mark %d at t=%.2f s, frames sent so far %u, calibrated ring offset %d\n", j->idx, j->t, j->sent, j->off);
		fprintf(o, "mark_%d_ring.raw: the driver's frame ring as it was: %zu bytes, rows of %zu bytes (1280 px, 16 bit), 4320 rows = 18 rows of slot pairs (240 rows each), left slot at x 0, right slot at x 640\n", j->idx, j->ringLen, g_line);
		fprintf(o, "mark_%d_frames.raw: %d frames we sent, %d x %d, 16 bit, newest first; numbers and age at the mark:\n", j->idx, j->count, g_W, g_H);
		for (int i = 0; i < j->count; i++)
			fprintf(o, "  %d: frame %u, sent %.0f ms before the mark\n", i, j->num[i], j->ageMs[i]);
		if (j->panel)
			fprintf(o, "mark_%d_panel.raw: /dev/fb1 as read at the mark, %zu bytes (480 x 854, 32 bit)\n", j->idx, j->panelLen);
		fclose(o);
	}
	printf("SLOTCHECK mark %d written (%.1f MB)\n", j->idx, ((double)j->ringLen + (double)j->count * g_W * g_H * 2 + (double)j->panelLen) / 1048576.0);
	fflush(stdout);
	free(j->ring);
	free(j->frames);
	free(j->panel);
	free(j);
	g_markBusy = 0;
	return NULL;
}

}   // namespace

void slotcheck_mark()
{
	if (!g_on || g_markBusy || g_marks >= MARK_MAX || !g_map)
		return;
	MarkJob *j = (MarkJob *)calloc(1, sizeof(MarkJob));
	if (!j)
		return;
	j->ringLen = g_line * 4320;
	if (j->ringLen > g_len)
		j->ringLen = g_len;
	j->ring = (uint8_t *)malloc(j->ringLen);
	j->frames = (uint16_t *)malloc((size_t)MARK_FRAMES * g_W * g_H * 2);
	if (!j->ring || !j->frames)
	{
		free(j->ring); free(j->frames); free(j);
		return;
	}
	uint64_t t0 = now_us();
	memcpy(j->ring, g_map, j->ringLen);   /* first: the ring keeps turning */
	j->sent = g_sent;
	for (uint32_t k = 1; k <= (uint32_t)MARK_FRAMES + 8 && j->count < MARK_FRAMES && k <= j->sent; k++)
	{
		const FrameRec *f = frame_at(j->sent - k);
		if (!f)
			continue;
		memcpy(j->frames + (size_t)j->count * g_W * g_H, f->px, (size_t)g_W * g_H * 2);
		j->num[j->count] = f->n;
		j->ageMs[j->count] = (t0 - f->t) / 1000.0;
		j->count++;
	}
	if (!g_fake)
	{
		int fd = open("/dev/fb1", O_RDONLY);
		if (fd >= 0)
		{
			size_t want = 480u * 854u * 4u;
			j->panel = (uint8_t *)malloc(want);
			if (j->panel)
			{
				ssize_t r = pread(fd, j->panel, want, 0);
				if (r > 0) j->panelLen = (size_t)r;
				else { free(j->panel); j->panel = NULL; }
			}
			close(fd);
		}
	}
	j->t = since_start();
	j->off = g_offPub;
	j->idx = g_marks++;
	g_markBusy = 1;
	printf("SLOTCHECK MARK %d at t=%.1f s, frame %u (the ring and %d frames copied in %.0f ms; writing them now)\n", j->idx, j->t, j->sent, j->count, (now_us() - t0) / 1000.0);
	fflush(stdout);
	pthread_t th;
	if (pthread_create(&th, NULL, mark_writer, j) == 0)
		pthread_detach(th);
	else
	{
		free(j->ring); free(j->frames); free(j->panel); free(j);
		g_markBusy = 0;
	}
}

void slotcheck_stop()
{
	if (!g_on)
		return;
	g_stop = true;
	if (g_threadOn)
		pthread_join(g_th, NULL);
	printf("SLOTCHECK stopped at t=%.1f s (the frames sent so far: %u)\n", since_start(), (unsigned)g_sent);
	fflush(stdout);
	g_on = false;
}

#else
bool slotcheck_start() { return false; }
void slotcheck_frame(const uint16_t *, int, int) {}
void slotcheck_after_present(const uint16_t *, int, int) {}
void slotcheck_mark() {}
void slotcheck_stop() {}
#endif
