#!/bin/bash
# Serialize every repository-managed Dreamcast loader session across agents.
#
# The console is shared hardware. This wrapper is the only sanctioned way to
# launch from this repository. It refuses to launch while any kos-tool process
# exists, records its owner, and removes the lock after the loader exits.
#
# mGBADC uses its own lock path. When both projects may touch the same console,
# point this wrapper at the same lock mGBADC uses:
#   DC_CONSOLE_LOCK=/tmp/mgbadc-dreamcast-console.lock
set -u
LOCK=${DC_CONSOLE_LOCK:-/tmp/dreamcast-console.lock}

if [ "${1:-}" = --clear-stale ]; then
	if pgrep -x kos-tool >/dev/null; then
		echo "refusing stale-lock clear: a kos-tool process exists" >&2
		pgrep -ax kos-tool >&2
		exit 1
	fi
	if [ ! -d "$LOCK" ]; then echo "no console lock exists"; exit 0; fi
	cat "$LOCK/owner" 2>/dev/null || true
	rm -f "$LOCK/owner" && rmdir "$LOCK" || exit 1
	echo "cleared stale console lock (no kos-tool process found)"
	exit 0
fi

if [ "$#" -eq 0 ]; then echo "usage: SDL_DC_LOCK_TASK=label $0 kos-tool-args..." >&2; exit 2; fi
TASK=${SDL_DC_LOCK_TASK:-unspecified}
if pgrep -x kos-tool >/dev/null; then
	echo "refusing launch: a kos-tool process is already running outside this lock" >&2
	pgrep -ax kos-tool >&2
	exit 3
fi
if ! mkdir "$LOCK" 2>/dev/null; then
	echo "Dreamcast console lock is held: $LOCK" >&2
	cat "$LOCK/owner" 2>/dev/null >&2 || true
	exit 3
fi
printf 'pid=%s\ntask=%s\nutc=%s\ncommand=' "$$" "$TASK" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$LOCK/owner"
printf '%q ' "$@" >> "$LOCK/owner"
printf '\n' >> "$LOCK/owner"
cleanup() {
	if [ "$(sed -n 's/^pid=//p' "$LOCK/owner" 2>/dev/null)" = "$$" ]; then
		rm -f "$LOCK/owner"
		rmdir "$LOCK" 2>/dev/null || true
	fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
kos-tool "$@"
exit $?
