/* Test hooks. The game can be driven from the environment for automatic tests: DM_AUTOMAP plays a map by itself, DM_PAD scripts the pad,
 * DM_SHOTDIR / DM_SHOTS save frames, DM_MONEY, DM_SOLDIERS, DM_FAKE_RESULT and many more (the list is in DEV-NOTES.md), and DMCFG_<key>
 * overrides a line of defensemania.cfg. All of them are read through dm_env(). A release build (the one that is shipped) does not define
 * DM_TESTHOOKS, so dm_env() is always NULL, every hook is dead code and the compiler drops it, strings included. Build with DEV=1
 * (build_sf3500.sh) or -DDM_TESTHOOKS (the native test build) to get them. */
#ifndef DM_DEVHOOKS_HPP
#define DM_DEVHOOKS_HPP

#include <stdlib.h>

#ifdef DM_TESTHOOKS
static inline const char *dm_env(const char *name) { return getenv(name); }
#else
static inline const char *dm_env(const char *) { return NULL; }
#endif

#endif
