#!/usr/bin/env bash
# Run one test exe under Wine in a throw-away, HEADLESS sandbox.
#   tools/wine_run.sh build/<area>.exe [name-filter]
# - fresh copy of the base prefix per run (tests cannot pollute each other or your real prefix)
# - no X: DISPLAY/WAYLAND_DISPLAY are unset so Wine uses its built-in null display driver; this keeps
#   the main X session untouched (many parallel wine runs with persistent explorer.exe exhausted the X
#   server's client slots before). Set W32KT_DISPLAY=xephyr to run inside a nested Xephyr server instead
#   (only for tests that genuinely need a display driver); the nested server is torn down afterwards.
# - hard timeout, and the prefix's wineserver and children are killed + the prefix removed afterwards.
set -uo pipefail
EXE="$1"; FILTER="${2:-}"
WINE="${WINE:-wine}"
command -v "$WINE" >/dev/null || { echo "wine not found (set WINE)" >&2; exit 1; }
# Shared hosts: at most ${W32KT_SLOTS:-3} wine runs at once, each in a memory-capped scope.
if [ -z "${W32KT_CAPPED:-}" ]; then
  export W32KT_CAPPED=1
  for s in $(seq 1 "${W32KT_SLOTS:-3}"); do
    exec 9>"/tmp/w32kt_wine_slot.$s"; flock -n 9 && exec systemd-run --user --scope --quiet -p MemoryMax=${W32KT_MEM_GB:-3}G -p MemorySwapMax=0 "$0" "$@"
  done
  exec 9>"/tmp/w32kt_wine_slot.1"; flock 9; exec systemd-run --user --scope --quiet -p MemoryMax=${W32KT_MEM_GB:-3}G -p MemorySwapMax=0 "$0" "$@"
fi
BASE="${W32KT_BASE_PREFIX:-$HOME/.cache/win32kt/prefix}"
TMO="${W32KT_TIMEOUT:-600}"
[ -d "$BASE/drive_c" ] || "$(dirname "$0")/make_base_prefix.sh" >&2
RUN="$(mktemp -d "${TMPDIR:-/tmp}/tp_prefix.XXXXXX")"
cp -a --reflink=auto "$BASE/." "$RUN/"
cleanup() {
  WINEPREFIX="$RUN" "$(dirname "$(command -v "$WINE")")/wineserver" -k 2>/dev/null
  sleep 0.3
  rm -rf "$RUN"
  [ -n "${XEPID:-}" ] && kill "$XEPID" 2>/dev/null
}
trap cleanup EXIT
ENVV=(WINEPREFIX="$RUN" WINEARCH=win64 WINEDEBUG="${WINEDEBUG:--all}")
if [ "${W32KT_DISPLAY:-none}" = "xephyr" ]; then
  N=$((100 + RANDOM % 800)); Xephyr ":$N" -screen 1024x768 -nolisten tcp >/dev/null 2>&1 & XEPID=$!
  sleep 1; ENVV+=(DISPLAY=":$N")
  env -u WAYLAND_DISPLAY "${ENVV[@]}" timeout "$TMO" "$WINE" "$EXE" ${FILTER:+"$FILTER"}
else
  env -u DISPLAY -u WAYLAND_DISPLAY "${ENVV[@]}" timeout "$TMO" "$WINE" "$EXE" ${FILTER:+"$FILTER"}
fi
