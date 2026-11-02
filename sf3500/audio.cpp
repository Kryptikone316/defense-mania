#include "audio.hpp"
#include "gfx.hpp"
#include "sf3500_mixer.hpp"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <map>
#include <vector>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.h"

static bool g_ok;
static bool g_enabled = true;
static int g_volume = 60;
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

static void drop()
{
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

static float gain() { return g_volume / 100.0f * 0.9f * g_currentGain; }

static void start_track(const std::string &path)
{
	drop();
	int err = 0;
	g_vorbis = stb_vorbis_open_filename(path.c_str(), &err, NULL);
	if (!g_vorbis)
	{
		printf("music: cannot decode %s (error %d)\n", path.c_str(), err);
		fflush(stdout);
		return;
	}
	stb_vorbis_info info = stb_vorbis_get_info(g_vorbis);
	g_channels = info.channels;
	g_rate = (int)info.sample_rate;
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
	g_enabled = cfg_num("music", 1) != 0;
	g_volume = (int)cfg_num("music_volume", 60);
}

void audio_shutdown()
{
	drop();
	if (g_ok)
		sf3500_mixer_deinit();
	g_ok = false;
}

void music_play_folder(const std::string &folder)
{
	if (!g_ok || folder == g_folder)
		return;
	g_folder = folder;
	drop();
	scan(folder);
	pick_and_play();
}

void music_update()
{
	if (!g_ok)
		return;
	if (g_enabled && g_vorbis && g_voice && !sf3500_voice_playing(g_voice))
		pick_and_play();
	if (g_enabled && g_audioSeconds >= 60 && millis() - g_logStamp > 1000)
	{
		printf("music: decoding takes %.0f%% of the time it plays (%.1f s of audio)\n", 100 * g_decodeSeconds / g_audioSeconds, g_audioSeconds);
		fflush(stdout);
		g_decodeSeconds = g_audioSeconds = 0;
		g_logStamp = millis();
	}
}

void music_set_enabled(bool on)
{
	if (on == g_enabled)
		return;
	g_enabled = on;
	if (!g_ok)
		return;
	if (!on)
		drop();
	else
		pick_and_play();
}

void music_set_volume(int percent)
{
	g_volume = percent < 0 ? 0 : percent > 100 ? 100 : percent;
	if (g_voice)
		sf3500_voice_set_gain(g_voice, gain());
}

bool music_enabled() { return g_enabled; }
int music_volume() { return g_volume; }
