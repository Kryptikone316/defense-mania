#!/bin/sh
set -u

PORT_DIR=/mnt/sdcard/roms/ports/defensemania
cd "$PORT_DIR"
rm -f /mnt/sdcard/cubegm/port_error.txt

# Settings (defensemania.cfg) are read from the port folder; the bundled SDL_ttf lives next to the executable.
export HOME="$PORT_DIR"
export LD_LIBRARY_PATH="$PORT_DIR:${LD_LIBRARY_PATH:-}"
# SDL only supplies timers and software surfaces; frames and the pad are handled by the port's own
# device modules.
export SDL_VIDEODRIVER=dummy
export SDL_NOMOUSE=1

# The port's own output goes to log.txt in the port folder, ending with an exit code line,
# so a problem can be diagnosed from the card.
rc=0
"$PORT_DIR/defensemania.mipsel" < /dev/null > log.txt 2>&1 || rc=$?
echo "EXIT CODE: $rc" >> log.txt
exit "$rc"
