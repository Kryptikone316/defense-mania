/* Background music for Defense Mania on the SF3500 mixer: one folder of Ogg Vorbis tracks per kind of
 * map, a random track from it, then another random one when it ends. */
#ifndef DM_AUDIO_HPP
#define DM_AUDIO_HPP

#include <string>

void audio_init();                                  /* the mixer; silent when there is no sound device */
void audio_shutdown();
void music_play_folder(const std::string &folder);  /* music/<folder>/*.ogg; keeps playing if it is already this folder */
void music_update();                                /* once a frame: starts the next track when one ends */
void music_set_enabled(bool on);
void music_set_volume(int percent);                 /* 0..100 */
bool music_enabled();
int music_volume();

#endif
