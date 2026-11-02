/* Background music for Defense Mania on the SF3500 mixer: one folder of Ogg Vorbis tracks per kind of
 * map, a random track from it, then another random one when it ends. */
#ifndef DM_AUDIO_HPP
#define DM_AUDIO_HPP

#include <string>

void audio_init();                                  /* the mixer; silent when there is no sound device */
void audio_shutdown();
void music_play_folder(const std::string &folder);  /* music/<folder>/*.ogg; keeps playing if it is already this folder */
void music_update();                                /* once a frame: starts the next track when one ends */
void music_set_volume(int percent);                 /* 0..100, 0 = no music */
int music_volume();

/* Sound effects: short WAV files in data/sfx, played over the music. */
enum {
	SFX_BUILD, SFX_ERROR, SFX_ARROW, SFX_CATAPULT, SFX_FREEZE, SFX_DIE, SFX_LIFELOST, SFX_UPGRADE, SFX_SELL,
	SFX_REPAIR, SFX_CRUMBLE, SFX_WAVE, SFX_TICK, SFX_GO, SFX_WIN, SFX_LOSE, SFX_MOVE, SFX_SELECT, SFX_COUNT
};
void sfx_play(int id);
void sfx_set_volume(int percent);                   /* 0..100 */
int sfx_volume();

/* One volume for everything, on top of the two above. */
void audio_set_master(int percent);                 /* 0..100 */
int audio_master();

#endif
