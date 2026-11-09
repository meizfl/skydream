#!/bin/sh
# Regenerates gen/ (recompiled C) from your own Skyrim files.
# Usage: tools/regen_gen.sh GAME_DIR
# GAME_DIR must contain TESV_unpacked.exe (TESV.exe with SteamStub removed by
# Steamless). Helper DLLs are native. See the README.
# Needs Python 3.10+: pip install capstone==5.0.9 pefile==2024.8.26
set -e
G=$(cd "${1:?usage: tools/regen_gen.sh GAME_DIR}" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
find_file() { for f in "$G"/*; do
    [ "$(echo "${f##*/}" | tr A-Z a-z)" = "$1" ] && { echo "$f"; return; }; done
    echo "missing $1 in $G" >&2; exit 1; }
EXE=$(find_file tesv_unpacked.exe)
mkdir -p analysis gen
# Drop stale output from the former PE-backed X3DAudio module.
rm -f gen/x3da_*.c gen/funcs_x3da.h gen/bink_*.c gen/funcs_bink.h gen/d3dx9_*.c gen/funcs_d3dx9.h
python3 tools/write_inputs.py analysis/linux-inputs.json "$EXE"
export LIFT_GAME=skyrim
# discover.py imports peimg.py from tools/, so run it from there.
(cd tools && python3 discover.py "$EXE" ../analysis/functions.json)
python3 tools/lift.py "$EXE" analysis/functions.json gen exe
python3 tools/deduplicate_generated.py gen --apply --report analysis/deduplication.json
python3 tools/embed_game_image.py "$EXE" gen/image
echo "gen/ is ready"
