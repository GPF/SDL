#!/bin/bash
# Re-baseline helper for the 2026-10-06 front-buffer capture work.
#
# Builds one fixture with capture enabled against an explicit SDL3 prefix,
# verifies the capture define reached the compiler, deletes any previous
# capture, runs the fixture through the shared locked console launcher, and
# files the log and capture under test/dreamcast/results/phase1-captures/.
# No `set -u`: KOS environ.sh reads KOS_SUBARCH before it is set.

REPO=/home/gpf/code/dreamcast/SDL3
CONSOLE=192.168.0.128
RESULTS=$REPO/test/dreamcast/results/phase1-captures
BUILDLOGS=$REPO/logs

source /opt/toolchains/dc/kos/environ.sh

# The fixture Makefiles default to the KOS addon tree, which is stale. Point
# both the include path and the library search path at the explicit prefix.
BASE_KOS_CFLAGS=$KOS_CFLAGS
BASE_KOS_LDFLAGS=$KOS_LDFLAGS

build() {
    local fixture="$1" prefix="$2" extra="${3:-}"
    local dir="$REPO/test/dreamcast/$fixture"
    local log="$BUILDLOGS/rebaseline_build_${fixture}${extra:+_${extra}}.log"

    export KOS_CFLAGS="$BASE_KOS_CFLAGS -I$prefix/include"
    export KOS_LDFLAGS="-L$prefix/lib/dreamcast $BASE_KOS_LDFLAGS"

    echo "== build $fixture against $prefix"
    ( cd "$dir" && rm -f *.elf *.o && \
      make -B -f Makefile.dc SCREENSHOT=1 \
        SDL3_PREFIX="$prefix" \
        SDL3_CONFIG_INCLUDEDIR="$prefix/include" \
        KOS_WRAPPERS_VERBOSE=1 $extra ) > "$log" 2>&1
    status=$?
    if [ $status -ne 0 ]; then
        echo "BUILD FAILED (status $status); log: $log"
        tail -20 "$log"
        return $status
    fi
    local elf
    elf=$(ls "$dir"/*.elf 2>/dev/null | head -1)
    if [ -z "$elf" ]; then
        echo "no ELF produced by $fixture"
        return 1
    fi
    if strings "$elf" | grep -q "front buffer capture"; then
        echo "OK $(basename "$elf"): front buffer capture present"
    else
        echo "MISSING capture string in $(basename "$elf")"
        return 1
    fi
    return 0
}

run() {
    local fixture="$1" elf="$2" label="$3"
    local dir="$REPO/test/dreamcast/$fixture"
    local log="$RESULTS/rebaseline_${label}.log"

    ( cd "$dir" && rm -f *.ppm && \
      SDL_DC_LOCK_TASK="$label" "$REPO/tools/dc-load/kos-tool-locked.sh" \
        -t "$CONSOLE" -m . -x "$elf" ) > "$log" 2>&1
    status=$?
    echo "== run $label exit=$status log=$log"
    local n
    n=$(ls "$dir"/*.ppm 2>/dev/null | wc -l)
    ( cd "$dir" && for f in *.ppm; do
        [ -e "$f" ] || continue
        if [ "$n" -eq 1 ]; then
            mv "$f" "$RESULTS/rebaseline_${label}.ppm"
        else
            mv "$f" "$RESULTS/rebaseline_${label}_${f}"
        fi
      done )
    grep -E "capture:|result=|PASS|FAIL|Program returned" "$log" | head -20
    return $status
}
