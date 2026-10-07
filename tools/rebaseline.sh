#!/bin/bash
# Re-baseline helper for the 2026-10-06 front-buffer capture work.
#
# Builds one fixture with capture enabled against an explicit SDL3 prefix,
# verifies the capture define reached the compiler, deletes any previous
# capture, runs the fixture through the shared locked console launcher, and
# files the log and capture under test/dreamcast/results/phase1-captures/.
# No `set -u`: KOS environ.sh reads KOS_SUBARCH before it is set.
#
# Usage (source it, then call):
#   build FIXTURE PREFIX [NAME=value | make-target ...]
#   run   FIXTURE ELF LABEL
# FIXTURE is a directory under test/dreamcast/, or an alias in resolve_fixture
# (pvr_dt_diag builds from pvr_dt_direct/). Extra build arguments must be make
# variables (NAME=value) or existing make targets such as p1a; anything else is
# rejected before the build starts. LABEL only names the output files.

REPO=/home/gpf/code/dreamcast/SDL3
CONSOLE=192.168.0.128
RESULTS=$REPO/test/dreamcast/results/phase1-captures
BUILDLOGS=$REPO/logs

source /opt/toolchains/dc/kos/environ.sh

# The fixture Makefiles default to the KOS addon tree, which is stale. Point
# both the include path and the library search path at the explicit prefix.
BASE_KOS_CFLAGS=$KOS_CFLAGS
BASE_KOS_LDFLAGS=$KOS_LDFLAGS

# Fixture names normally match test/dreamcast/<name>/. A few fixtures are extra
# executables built from another fixture's directory; they are listed here so that
# `build pvr_dt_diag ...` works by name. Sets FX_DIR, FX_ELF (a specific ELF name,
# empty = whatever the directory's default build produces).
resolve_fixture() {
    case "$1" in
        pvr_dt_diag) FX_DIR=$REPO/test/dreamcast/pvr_dt_direct; FX_ELF=pvr_dt_diag.elf ;;
        *)           FX_DIR=$REPO/test/dreamcast/$1; FX_ELF= ;;
    esac
    if [ ! -f "$FX_DIR/Makefile.dc" ]; then
        echo "unknown fixture '$1': no $FX_DIR/Makefile.dc" >&2
        return 2
    fi
}

# build FIXTURE PREFIX [MAKE_ARG...]
# MAKE_ARG is either NAME=value (a make variable) or an existing make target
# (for example p1a for pvr_phase1_hardening). Anything else is an error, so a
# label can never silently become a target. Log names are sanitised.
build() {
    local fixture="$1" prefix="$2"
    [ $# -ge 2 ] || { echo "usage: build FIXTURE PREFIX [MAKE_ARG...]" >&2; return 2; }
    shift 2
    resolve_fixture "$fixture" || return 2
    local dir=$FX_DIR want_elf=$FX_ELF
    local tag
    tag=$(printf '%s' "$*" | tr -c 'A-Za-z0-9_.\n-' '_')
    local log="$BUILDLOGS/rebaseline_build_${fixture}${tag:+_${tag}}.log"
    local -a vars=() targets=()
    local arg
    for arg in "$@"; do
        case "$arg" in
            [A-Za-z_]*=*) vars+=("$arg") ;;
            -*|*[!A-Za-z0-9_./-]*) echo "bad make argument '$arg'" >&2; return 2 ;;
            *) targets+=("$arg") ;;
        esac
    done
    [ -n "$want_elf" ] && [ ${#targets[@]} -eq 0 ] && targets=("$want_elf")

    export KOS_CFLAGS="$BASE_KOS_CFLAGS -I$prefix/include"
    export KOS_LDFLAGS="-L$prefix/lib/dreamcast $BASE_KOS_LDFLAGS"

    local -a mk=(make -f Makefile.dc SCREENSHOT=1 "SDL3_PREFIX=$prefix"
                 "SDL3_CONFIG_INCLUDEDIR=$prefix/include" KOS_WRAPPERS_VERBOSE=1
                 "${vars[@]}")
    # Dry run first so an unknown target is reported as such, not as a build failure.
    local dry
    dry=$( cd "$dir" && "${mk[@]}" -n -B "${targets[@]}" 2>&1 ) || {
        echo "BUILD ARGUMENTS REJECTED for $fixture: ${targets[*]:-(default target)}" >&2
        printf '%s\n' "$dry" | tail -3 >&2
        return 2
    }

    echo "== build $fixture against $prefix"
    if [ -n "$want_elf" ]; then
        ( cd "$dir" && rm -f "$want_elf" )
    else
        ( cd "$dir" && rm -f *.elf *.o )
    fi
    ( cd "$dir" && "${mk[@]}" -B "${targets[@]}" ) > "$log" 2>&1
    local status=$?
    if [ $status -ne 0 ]; then
        echo "BUILD FAILED (status $status); log: $log"
        tail -20 "$log"
        return $status
    fi
    local elf
    if [ -n "$want_elf" ]; then
        elf=$dir/$want_elf
    else
        elf=$(ls "$dir"/*.elf 2>/dev/null | head -1)
    fi
    if [ -z "$elf" ] || [ ! -f "$elf" ]; then
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
    resolve_fixture "$fixture" || return 2
    local dir=$FX_DIR
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
