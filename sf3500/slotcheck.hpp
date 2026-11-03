/* In-game check of what the display driver makes of our frames (see slotcheck.cpp). Off unless slot_check = 1 in
 * defensemania.cfg (or DMCFG_slot_check=1 in the environment). */
#ifndef DM_SLOTCHECK_HPP
#define DM_SLOTCHECK_HPP

#include <stdint.h>

bool slotcheck_start();                                      /* maps /dev/fb0 and starts the reader thread; false when off or not possible */
void slotcheck_frame(const uint16_t *canvas, int w, int h);  /* every frame, just before it goes to the driver */
void slotcheck_after_present(const uint16_t *canvas, int w, int h);   /* right after: only used by the emulator's fake display */
void slotcheck_stop();

#endif
