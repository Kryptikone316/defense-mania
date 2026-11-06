#!/bin/sh
# Builds defensemania.mipsel for the Data Frog SF3500 (MIPS32r2 74Kc, hard float, SDL 1.2 on the device).
#
#   MIPS_PREFIX=/path/to/mips-mti-linux-gnu- SYSROOT=/path/to/sysroot SDL_TTF_DIR=/path/to/SDL_ttf-1.2.11-build ./build_sf3500.sh
#
#   add DEV=1 for the test build (defensemania-dev.*, with the DM_* test hooks); the plain build is the release.
#
# -O2 only: no -ffast-math and no -mdspr2 (a device-only fault was traced to them in the Quake ports).
set -eu
: "${MIPS_PREFIX:?set MIPS_PREFIX, e.g. /opt/tc/bin/mips-mti-linux-gnu-}"
: "${SYSROOT:?set SYSROOT}"
: "${SDL_TTF_DIR:?set SDL_TTF_DIR to the folder holding SDL_ttf.h and libSDL_ttf-2.0.so.0}"

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CXX=${CXX:-${MIPS_PREFIX}g++}
STRIP=${STRIP:-${MIPS_PREFIX}strip}
cd "$HERE"   # compile with relative paths, so the binary does not depend on where the tree is

DEVFLAG=""; OUTNAME=defensemania
if [ "${DEV:-0}" = 1 ]; then DEVFLAG="-DDM_TESTHOOKS"; OUTNAME=defensemania-dev; fi   # DEV=1: a test build with the DM_* hooks (see DEV-NOTES.md)
FLAGS="-EL -mips32r2 -mtune=74kc -mhard-float --sysroot=$SYSROOT -O2 -DSF3500 $DEVFLAG -Wall -Wno-unused-result"
OBJ=obj-sf3500; [ "${DEV:-0}" = 1 ] && OBJ=obj-sf3500-dev
mkdir -p "$OBJ"
INC="-Isf3500 -I$SYSROOT/usr/include/SDL -I$SDL_TTF_DIR"

for f in sf3500/*.cpp; do
	o="$OBJ/$(basename "$f" .cpp).o"
	if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find sf3500 -name '*.h*' -newer "$o" -print -quit)" ]; then
		$CXX $FLAGS -std=gnu++14 $INC -c "$f" -o "$o"
	fi
done
$CXX $FLAGS -o "$HERE/$OUTNAME.mipsel" "$OBJ"/*.o "$SDL_TTF_DIR/libSDL_ttf-2.0.so.0" -lSDL -lpthread -ldl -lm
$STRIP -o "$HERE/$OUTNAME.stripped.mipsel" "$HERE/$OUTNAME.mipsel"
echo "built $HERE/$OUTNAME.stripped.mipsel"
