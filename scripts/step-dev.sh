#!/usr/bin/env bash
# The STEP exporter's development loop, in one place, so no session rewrites it.
#
# Every session on this exporter used to rebuild the same five throwaway scripts
# in git-ignored build/ - build and stage, run the suite with the staging DLLs on
# PATH, export the reference parts with their customizer set, run the census -
# because a command passed inline from PowerShell crosses two quoting rules and
# loses its quotes, and lid10's `-P "New set 1"` is exactly the argument that
# breaks. This is those scripts, committed, and resolving the repository from
# its own location so it works in any worktree.
#
#   scripts/step-dev.sh build              incremental build and stage (no configure)
#   scripts/step-dev.sh test               the STEP suite: export-step-|mutations, 58 tests
#   scripts/step-dev.sh flagship           the three flagship tests only
#   scripts/step-dev.sh export <part> [dir]
#                                          lid10 | bayonet | f02 | f04 | <file.scad>
#                                          into [dir] (default build/step-dev), with
#                                          the right customizer, TWICE: <name>.stp with
#                                          both feature flags and <name>-exact.stp with
#                                          the analytic flag alone. Measure both - a
#                                          change can improve one tier and wreck the
#                                          other. STEP_DEV_TIER=approx skips the second.
#   scripts/step-dev.sh census <file.stp>...
#                                          scripts/step-diagnostics/fault-census.py
#   scripts/step-dev.sh python             print the interpreter ctest uses (has OCP)
#
# From PowerShell, go through scripts/step-dev.ps1, which sets up MSYS2 correctly.

set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
# The two tiers, and both of them get exported.
#
# The exact tier writes only what it can prove; the approximation tier is
# allowed to spend the tessellation band. They are different exports and a
# change can improve one while wrecking the other - measured on 2026-09-22, a
# region rule bought 110 exact circles with both flags on and took a declared
# cone on lid10 down to planes with only the analytic one, on a run that was
# green. This used to export the approximation tier alone, so the tier most
# likely to be damaged was the one nobody looked at. See doc/step-rungs.md.
EXACT_FLAGS=(--enable=step-analytic-surfaces)
FLAGS=(--enable=step-analytic-surfaces --enable=step-approximate-surfaces)
BIN="$ROOT/build/staging/pythonscad.com"

# How many tests to run at once: the whole machine. This was -j8, a number
# someone typed, which left half of a 16-core box idle for the ~20 minutes the
# suite takes.
#
# It is deliberately NOT the three-quarters rule scripts/msys2-build.sh uses.
# That rule is there because a CGAL translation unit peaks near 2 GB and memory,
# not cores, is what binds a *build*; it is also what gives the 4-core agent
# sandbox its -j3. ctest runs exports, not compiles, and nothing here comes near
# that footprint - so the suite gets every core, and the build keeps its
# fraction. Override with STEP_DEV_JOBS.
JOBS="${STEP_DEV_JOBS:-}"
if [ -z "$JOBS" ]; then
  JOBS="$(nproc 2>/dev/null || echo 4)"
  [ "$JOBS" -lt 1 ] && JOBS=1
fi

# The interpreter CMake found, which is the one with cadquery-ocp installed -
# not MSYS2's python, which has no OCP and makes every (OCP) instrument skip.
ocp_python() {
  grep -ao 'Python3_EXECUTABLE:INTERNAL=[^;]*' "$ROOT/build/CMakeCache.txt" 2>/dev/null | cut -d= -f2
}

export_part() {
  local part="$1" out="${2:-$ROOT/build/step-dev}"
  mkdir -p "$out"
  # argv as an array: the customizer set name has a space in it, and this is the
  # one place that must never pass through another layer of quoting.
  local -a args
  case "$part" in
    lid10)   args=(examples/step_test/lid10.scad -p examples/step_test/lid10.json -P "New set 1") ;;
    bayonet) args=(examples/step_test/bayonet_container_v1-2.scad
                   -p examples/step_test/bayonet_container_v1-2.json -P "New set 1") ;;
    f02)     args=(tests/data/scad/step-export/step-band-family.scad -D FN=32) ;;
    f04)     args=(tests/data/scad/step-export/step-band-family.scad -D FN=64) ;;
    *.scad|*.py) args=("$part") ;;
    *) echo "unknown part: $part (lid10 | bayonet | f02 | f04 | <file.scad>)" >&2; return 2 ;;
  esac
  local name="${part##*/}"; name="${name%.*}"
  local target="$out/$name.stp" log="$out/$name.log"
  local exact="$out/$name-exact.stp" exact_log="$out/$name-exact.log"
  "$BIN" "${args[@]}" -o "$target" --trust-python "${FLAGS[@]}" >"$log" 2>&1
  local rc=$?
  echo "$name: exit $rc -> $target"
  grep -E "EXPORT-ERROR" "$log" && echo "   ^ the exporter says its own output is wrong" >&2
  # And the exact tier, always. Skipped only with STEP_DEV_TIER=approx, which is
  # for a loop that has already established the exact tier does not move.
  if [ "${STEP_DEV_TIER:-both}" != "approx" ]; then
    "$BIN" "${args[@]}" -o "$exact" --trust-python "${EXACT_FLAGS[@]}" >"$exact_log" 2>&1
    local erc=$?
    echo "$name: exit $erc -> $exact   (exact tier, --enable=step-analytic-surfaces alone)"
    grep -E "EXPORT-ERROR" "$exact_log" && echo "   ^ the exporter says its own output is wrong" >&2
    [ $rc -eq 0 ] && rc=$erc
  fi
  return $rc
}

case "${1:-}" in
  build)
    bash "$ROOT/scripts/msys2-build.sh" 2>&1 | tail -3
    ;;
  test)
    export PATH="$ROOT/build/staging:$PATH"
    ctest --test-dir "$ROOT/build" -R 'export-step-|mutations' -j"$JOBS" 2>&1 | tail -12
    ;;
  flagship)
    export PATH="$ROOT/build/staging:$PATH"
    ctest --test-dir "$ROOT/build" -R 'export-step-flagship' -j"$JOBS" --output-on-failure 2>&1 | tail -30
    ;;
  export)
    shift
    [ $# -ge 1 ] || { echo "export what? lid10 | bayonet | f02 | f04 | <file.scad>" >&2; exit 2; }
    export_part "$@"
    ;;
  census)
    shift
    py="$(ocp_python)"
    "${py:-python}" "$ROOT/scripts/step-diagnostics/fault-census.py" "$@"
    ;;
  python)
    ocp_python
    ;;
  *)
    sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
    ;;
esac
