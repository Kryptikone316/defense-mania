#!/bin/sh
# Builds defensemania.mipsel for the Data Frog SF3500 (MIPS32r2 74Kc, hard float, SDL 1.2 on the device).
#
#   MIPS_PREFIX=/path/to/mips-mti-linux-gnu- SYSROOT=/path/to/sysroot SDL_TTF_DIR=/path/to/SDL_ttf-1.2.11-build ./build_sf3500.sh
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
OBJ=obj-sf3500
mkdir -p "$OBJ"

FLAGS="-EL -mips32r2 -mtune=74kc -mhard-float --sysroot=$SYSROOT -O2 -DSF3500 -Wall -Wno-unused-result"
INC="-Isf3500 -I$SYSROOT/usr/include/SDL -I$SDL_TTF_DIR"

for f in sf3500/*.cpp; do
	o="$OBJ/$(basename "$f" .cpp).o"
	if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find sf3500 -name '*.h*' -newer "$o" -print -quit)" ]; then
		$CXX $FLAGS -std=gnu++14 $INC -c "$f" -o "$o"
	fi
done
$CXX $FLAGS -o "$HERE/defensemania.mipsel" "$OBJ"/*.o "$SDL_TTF_DIR/libSDL_ttf-2.0.so.0" -lSDL -lpthread -ldl -lm
$STRIP -o "$HERE/defensemania.stripped.mipsel" "$HERE/defensemania.mipsel"
echo "built $HERE/defensemania.stripped.mipsel"
