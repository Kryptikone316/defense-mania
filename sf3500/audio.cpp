#include "audio.hpp"
#include "devhooks.hpp"
#include "gfx.hpp"
#include "sf3500_mixer.hpp"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>
#include <map>
#include <vector>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.h"

static bool g_ok;
static int16_t *g_sfxPcm[SFX_COUNT];   /* 48 kHz stereo */
static int g_sfxFrames[SFX_COUNT];
static uint32_t g_sfxLast[SFX_COUNT];
static Sf3500Voice *g_sfxVoices[24];
static int g_sfxActive;

static bool g_enabled = true;
static int g_volume = 60, g_master = 80, g_sfxVolume = 80;
static std::string g_folder, g_current;
static std::vector<std::string> g_tracks;
static Sf3500Voice *g_voice;
static stb_vorbis *g_vorbis;
static int g_channels;
static int g_lastIndex = -1;
static std::map<std::string, float> g_trackGain;   /* "<folder>/<file>" -> linear gain, from music/loudness.txt */
static float g_currentGain = 1.0f;

/* how much of the CPU the decoder takes, logged every minute so a slow device shows up in log.txt */
static double g_decodeSeconds, g_audioSeconds;
static int g_rate = 44100;
static uint32_t g_logStamp;

static double now_sec()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int fill(void *, int16_t *dst, int frames)
{
	double t0 = now_sec();
	int got = stb_vorbis_get_samples_short_interleaved(g_vorbis, g_channels, dst, frames * g_channels);
	g_decodeSeconds += now_sec() - t0;
	g_audioSeconds += (double)got / g_rate;
	return got;
}

/* Opening an Ogg file (reading it and building the decoder's tables) takes a good fraction of a second on this CPU.
 * It is done on a thread of its own so the game does not stop for it; music_update() picks the result up. */
static pthread_t g_loadThread;
static volatile int g_loadState;   /* 0 idle, 1 loading, 2 done */
static bool g_loadThreaded;
static std::string g_loadPath;
static stb_vorbis *g_loaded;
static int g_loadErr;

static void *load_main(void *onThread)
{
	if (onThread)   /* the game and the display come first */
		setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);
	int err = 0;
	g_loaded = stb_vorbis_open_filename(g_loadPath.c_str(), &err, NULL);
	g_loadErr = err;
	__sync_synchronize();
	g_loadState = 2;
	return NULL;
}

static void cancel_load()
{
	if (g_loadState == 0)
		return;
	if (g_loadThreaded)
		pthread_join(g_loadThread, NULL);
	g_loadThreaded = false;
	if (g_loaded)
		stb_vorbis_close(g_loaded);
	g_loaded = NULL;
	g_loadState = 0;
}

static void drop()
{
	cancel_load();
	if (g_voice)
	{
		sf3500_voice_destroy(g_voice);
		g_voice = NULL;
	}
	if (g_vorbis)
	{
		stb_vorbis_close(g_vorbis);
		g_vorbis = NULL;
	}
}

static float gain() { return g_master / 100.0f * g_volume / 100.0f * 0.9f * g_currentGain; }

static void begin_load(const std::string &path)
{
	drop();
	g_loadPath = path;
	g_loaded = NULL;
	g_loadState = 1;
	g_loadThreaded = pthread_create(&g_loadThread, NULL, load_main, (void *)1) == 0;
	if (!g_loadThreaded)
		load_main(NULL);   /* no thread to be had: do it here, as before */
}

/* the track has been opened: start playing it */
static void finish_load()
{
	if (g_loadThreaded)
		pthread_join(g_loadThread, NULL);
	g_loadThreaded = false;
	g_vorbis = g_loaded;
	g_loaded = NULL;
	g_loadState = 0;
	const std::string &path = g_loadPath;
	if (!g_vorbis)
	{
		printf("music: cannot decode %s (error %d)\n", path.c_str(), g_loadErr);
		fflush(stdout);
		return;
	}
	stb_vorbis_info info = stb_vorbis_get_info(g_vorbis);
	g_channels = info.channels;
	g_rate = (int)info.sample_rate;
	if (!g_ok)   /* test hook (no sound in the emulator): only show that the loader worked */
	{
		printf("music test: opened %s, %d channels\n", path.c_str(), g_channels);
		fflush(stdout);
		drop();
		return;
	}
	std::map<std::string, float>::const_iterator gi = g_trackGain.find(path.substr(6));   /* after "music/" */
	g_currentGain = gi == g_trackGain.end() ? 1.0f : gi->second;
	g_voice = sf3500_voice_create_stream(fill, NULL, g_rate, g_channels);
	if (!g_voice)
	{
		drop();
		return;
	}
	sf3500_voice_set_relative(g_voice, true);
	sf3500_voice_set_looping(g_voice, false);
	sf3500_voice_set_gain(g_voice, gain());
	sf3500_voice_play(g_voice);
	g_current = path;
	printf("music: %s (%.1f dB)\n", path.c_str(), 20 * log10f(g_currentGain));
	fflush(stdout);
}

static void start_track(const std::string &path) { begin_load(path); }

static void pick_and_play()
{
	if (g_tracks.empty() || !g_enabled)
		return;
	int i = rand() % (int)g_tracks.size();
	if (g_tracks.size() > 1 && i == g_lastIndex)
		i = (i + 1 + rand() % ((int)g_tracks.size() - 1)) % (int)g_tracks.size();
	g_lastIndex = i;
	start_track(g_tracks[i]);
}

static void scan(const std::string &folder)
{
	g_tracks.clear();
	g_lastIndex = -1;
	std::string dir = "music/" + folder;
	DIR *d = opendir(dir.c_str());
	if (!d)
		return;
	while (struct dirent *e = readdir(d))
	{
		size_t n = strlen(e->d_name);
		if (n > 4 && !strcasecmp(e->d_name + n - 4, ".ogg"))
			g_tracks.push_back(dir + "/" + e->d_name);
	}
	closedir(d);
}

static void sfx_load();

void audio_init()
{
	srand((unsigned)time(NULL));
	g_ok = sf3500_mixer_init();
	if (!g_ok)
	{
		printf("sound mixer not available, running silent\n");
		fflush(stdout);
	}
	FILE *lf = fopen("music/loudness.txt", "r");
	if (lf)
	{
		char line[300];
		while (fgets(line, sizeof line, lf))
		{
			char *eq = strrchr(line, '=');
			if (line[0] == '#' || !eq)
				continue;
			float db = (float)atof(eq + 1);
			while (eq > line && (eq[-1] == ' ' || eq[-1] == '\t')) eq--;
			*eq = 0;
			if (db > 0) db = 0;
			if (db < -12) db = -12;
			g_trackGain[line] = powf(10.0f, db / 20.0f);
		}
		fclose(lf);
	}
	g_master = (int)cfg_num("master_volume", 80);
	g_volume = (int)cfg_num("music_volume", 60);
	g_sfxVolume = (int)cfg_num("sfx_volume", 80);
	g_enabled = g_volume > 0;
	sfx_load();
}

void audio_shutdown()
{
	drop();
	for (int i = 0; i < g_sfxActive; i++)
		sf3500_voice_destroy(g_sfxVoices[i]);
	g_sfxActive = 0;
	if (g_ok)
		sf3500_mixer_deinit();
	g_ok = false;
}

void music_play_folder(const std::string &folder)
{
	if ((!g_ok && !dm_env("DM_MUSICTEST")) || folder == g_folder)
		return;
	g_folder = folder;
	drop();
	scan(folder);
	pick_and_play();
}

void music_update()
{
	if (!g_ok && !dm_env("DM_MUSICTEST"))
		return;
	static int stressCount;   /* test hook: a new track every 25 frames, to shake out thread problems */
	static const bool stress = dm_env("DM_MUSIC_STRESS") != NULL;
	if (stress && g_enabled && ++stressCount % 25 == 0)
		pick_and_play();
	if (g_loadState == 2)
		finish_load();
	else if (g_enabled && g_vorbis && g_voice && !sf3500_voice_playing(g_voice))
		pick_and_play();
	if (g_enabled && g_audioSeconds >= 60 && millis() - g_logStamp > 1000)
	{
		printf("music: decoding takes %.0f%% of the time it plays (%.1f s of audio)\n", 100 * g_decodeSeconds / g_audioSeconds, g_audioSeconds);
		fflush(stdout);
		g_decodeSeconds = g_audioSeconds = 0;
		g_logStamp = millis();
	}
}

static void apply_music_state()
{
	bool want = g_volume > 0 && g_master > 0;
	if (!g_ok)
	{
		g_enabled = want;
		return;
	}
	if (want && !g_enabled)
	{
		g_enabled = true;
		pick_and_play();
	}
	else if (!want && g_enabled)
	{
		g_enabled = false;
		drop();
	}
	if (g_voice)
		sf3500_voice_set_gain(g_voice, gain());
}

void music_set_volume(int percent)
{
	g_volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
	apply_music_state();
}
int music_volume() { return g_volume; }

void audio_set_master(int percent)
{
	g_master = percent < 0 ? 0 : percent > 100 ? 100 : percent;
	apply_music_state();
}
int audio_master() { return g_master; }

/* ------------------------------------------------------------------ sound effects */
static const char *sfx_names[SFX_COUNT] = {
	"build", "error", "arrow", "catapult", "freeze", "die", "lifelost", "upgrade", "sell",
	"repair", "crumble", "wave", "tick", "go", "win", "lose", "move", "select" };
static const int sfx_gap[SFX_COUNT] = { 0, 150, 70, 90, 90, 60, 150, 0, 0, 0, 100, 0, 0, 0, 0, 0, 25, 25 };   /* ms between two of the same */

static void sfx_load()
{
	for (int i = 0; i < SFX_COUNT; i++)
	{
		char path[96];
		snprintf(path, sizeof path, "data/sfx/%s.wav", sfx_names[i]);
		FILE *f = fopen(path, "rb");
		if (!f)
			continue;
		fseek(f, 0, SEEK_END);
		long size = ftell(f);
		fseek(f, 0, SEEK_SET);
		unsigned char *b = (unsigned char *)malloc(size);
		if (b && fread(b, 1, size, f) == (size_t)size && size > 44)
		{
			/* our own files: RIFF, a 16-byte "fmt " chunk (mono, 16 bit, 48 kHz), then "data" */
			long pos = 12;
			while (pos + 8 <= size && memcmp(b + pos, "data", 4) != 0)
				pos += 8 + (b[pos + 4] | b[pos + 5] << 8 | b[pos + 6] << 16) + ((b[pos + 4] & 1) ? 1 : 0);
			if (pos + 8 <= size)
			{
				int n = (int)((size - pos - 8) / 2);
				int16_t *pcm = (int16_t *)malloc((size_t)n * 4);
				if (pcm)
				{
					const int16_t *src = (const int16_t *)(b + pos + 8);
					for (int k = 0; k < n; k++)
						pcm[k * 2] = pcm[k * 2 + 1] = src[k];
					g_sfxPcm[i] = pcm;
					g_sfxFrames[i] = n;
				}
			}
		}
		free(b);
		fclose(f);
	}
}

void sfx_play(int id)
{
	if (!g_ok || id < 0 || id >= SFX_COUNT || !g_sfxPcm[id] || g_sfxVolume <= 0 || g_master <= 0)
		return;
	uint32_t now = millis();
	if (now - g_sfxLast[id] < (uint32_t)sfx_gap[id])
		return;
	g_sfxLast[id] = now;
	for (int i = 0; i < g_sfxActive; )   /* let go of the ones that have finished */
	{
		if (!sf3500_voice_playing(g_sfxVoices[i]))
		{
			sf3500_voice_destroy(g_sfxVoices[i]);
			g_sfxVoices[i] = g_sfxVoices[--g_sfxActive];
		}
		else
			i++;
	}
	if (g_sfxActive >= 24)
		return;
	Sf3500Voice *v = sf3500_voice_create_static(g_sfxPcm[id], g_sfxFrames[id]);
	if (!v)
		return;
	sf3500_voice_set_relative(v, true);
	sf3500_voice_set_gain(v, g_master / 100.0f * g_sfxVolume / 100.0f);
	sf3500_voice_play(v);
	g_sfxVoices[g_sfxActive++] = v;
}

void sfx_set_volume(int percent) { g_sfxVolume = percent < 0 ? 0 : percent > 100 ? 100 : percent; }
int sfx_volume() { return g_sfxVolume; }
