#!/bin/bash
# Differential host test of the triangle emit paths. Compares the working-tree
# SDL_render_dreamcast_pvr_geometry.c against a git revision (default HEAD).
# Usage: run.sh [revision] [iterations]
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
src=src/render/dreamcast/SDL_render_dreamcast_pvr_geometry.c
rev=${1:-HEAD}
iter=${2:-2000000}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git -C "$repo" show "$rev:$src" > "$tmp/old.c"
python3 -I "$here/extract.py" "$tmp/old.c" OLD_ > "$tmp/old_funcs.inc"
python3 -I "$here/extract.py" "$repo/$src" NEW_ > "$tmp/new_funcs.inc"
cp "$here/equivalence.c" "$tmp/"
# each side needs its own EmitVertex, defined before the extracted functions
sed -i 's|^#include "old_funcs.inc"|DEFINE_SIDE(OLD_)\n#include "old_funcs.inc"|; s|^#include "new_funcs.inc"|DEFINE_SIDE(NEW_)\n#include "new_funcs.inc"|' "$tmp/equivalence.c"
gcc -O2 -std=gnu11 -Wall -Wno-unused-function -Wno-unused-variable -I"$tmp" -o "$tmp/equiv" "$tmp/equivalence.c" -lm
"$tmp/equiv" "$iter"

# Negative control: perturb the new sprite's top-right x coordinate and require
# the QueueCopy record comparison to reject it.
sed 's/quad\[1\]\.x = maxx \* scale_x;/quad[1].x = (maxx + 1.0f) * scale_x;/' \
    "$tmp/new_funcs.inc" > "$tmp/new_broken_funcs.inc"
sed 's|^#include "new_funcs.inc"|#include "new_broken_funcs.inc"|' \
    "$tmp/equivalence.c" > "$tmp/broken_equivalence.c"
gcc -O2 -std=gnu11 -Wall -Wno-unused-function -Wno-unused-variable -I"$tmp" \
    -o "$tmp/broken_equiv" "$tmp/broken_equivalence.c" -lm
if "$tmp/broken_equiv" 1000 > "$tmp/broken.log" 2>&1; then
    echo "broken QueueCopy negative control unexpectedly passed" >&2
    exit 1
fi
if ! grep -Eq 'record_mismatches=[1-9][0-9]*' "$tmp/broken.log"; then
    cat "$tmp/broken.log" >&2
    echo "broken QueueCopy was not caught by the record comparison" >&2
    exit 1
fi
echo "negative control: deliberately broken QueueCopy was detected"
