#!/usr/bin/env bash
# Build, stage and test PythonSCAD in an MSYS2 UCRT64 shell.
#
# The native Windows build has three steps that are easy to get wrong and whose
# failures all look like something else:
#
#   1. A freshly linked build/pythonscad.exe cannot run. It needs the bundled
#      runtime Python and `cmake --install`, because python_mingw/ ships an
#      import library but no DLL. Without them the binary exits 127, or
#      0xC0000135 (3221225781) when Windows launches it.
#   2. ctest drives build/pythonscad.com, which has the same problem, so a bare
#      ctest fails every single test with what looks like a crash code.
#   3. The staging root, not staging/bin, is where the runnable exe lives.
#
# This script does them in order. Run it from inside an MSYS2 UCRT64 shell:
#
#   ./scripts/msys2-build.sh                 # build and stage, incrementally
#   ./scripts/msys2-build.sh --test          # ... and run the full suite
#   ./scripts/msys2-build.sh --test -R step  # ... a subset
#   ./scripts/msys2-build.sh -j8 --test      # override the job count
#   ./scripts/msys2-build.sh --configure     # after ADDING a file (see below)
#
# It does NOT configure by default, and that is deliberate: re-running configure
# regenerates the build files and everything is then out of date, so `cmake -B
# build` costs a FULL rebuild - about an hour here - every single time. Editing
# existing sources never needs it. Pass --configure only when you have added a
# file: sources are picked up by glob, and a newly added test needs a configure
# before ctest will see it at all.
#
# From PowerShell, drive it non-interactively with:
#
#   $env:MSYSTEM='UCRT64'; $env:CHERE_INVOKING='1'
#   C:\msys64\usr\bin\bash.exe -lc "cd /e/path/to/pythonscad && ./scripts/msys2-build.sh --test"
#
# The -l is required: the login profile is what puts /ucrt64/bin on PATH.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="build"
JOBS=""
RUN_TESTS=0
DO_CONFIGURE=0
CTEST_ARGS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --test) RUN_TESTS=1; shift ;;
    --configure) DO_CONFIGURE=1; shift ;;
    -j*) JOBS="${1#-j}"; shift ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    -h|--help) sed -n '2,36p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) CTEST_ARGS+=("$1"); shift ;;
  esac
done

if [ -z "$JOBS" ]; then
  # Memory is the binding constraint - a CGAL translation unit peaks near 2 GB -
  # so leave headroom rather than saturating the machine.
  ncpu="$(nproc 2>/dev/null || echo 4)"
  JOBS=$(( ncpu * 3 / 4 ))
  [ "$JOBS" -lt 1 ] && JOBS=1
fi

if [ "${MSYSTEM:-}" != "UCRT64" ]; then
  echo "warning: MSYSTEM is '${MSYSTEM:-unset}', expected UCRT64." >&2
  echo "         Start this from an MSYS2 UCRT64 shell, or set MSYSTEM=UCRT64." >&2
fi

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  echo "==> configure ($BUILD_DIR) - no cache yet"
  cmake -B "$BUILD_DIR"
elif [ "$DO_CONFIGURE" -eq 1 ]; then
  echo "==> configure ($BUILD_DIR) - asked for, expect a full rebuild"
  cmake -B "$BUILD_DIR"
else
  echo "==> skipping configure (pass --configure after adding a file)"
fi

echo "==> build -j$JOBS"
cmake --build "$BUILD_DIR" -j"$JOBS"

echo "==> bundle runtime python"
BUNDLE_PY_AUTO_INSTALL_PIP_LICENSES=1 \
  ./scripts/bundle-runtime-python.sh "$BUILD_DIR/pythonscad-bundled-py" --python python

echo "==> install to $BUILD_DIR/staging"
cmake --install "$BUILD_DIR" --prefix "$BUILD_DIR/staging"

STAGED="$ROOT/$BUILD_DIR/staging/pythonscad.exe"
if [ ! -x "$STAGED" ]; then
  echo "error: no staged binary at $STAGED" >&2
  exit 1
fi

echo "==> smoke test"
"$STAGED" --info | sed -n '1,3p'

if [ "$RUN_TESTS" -eq 1 ]; then
  # ctest runs build/pythonscad.com, not the staged binary, so the build tree has
  # to be made runnable. Three things are needed, and each one otherwise fails as
  # something that looks unrelated:
  #
  #   PATH        the staged DLLs. Without them every test "crashes" with
  #               status 3221225781, which is 0xC0000135, DLL not found.
  #   *.pyd       the 22 stdlib extension modules. The embedded interpreter
  #               resolves these relative to the executable, not via PYTHONHOME
  #               (which was tried and does not work), so they have to sit
  #               beside pythonscad.com. Without them, tests that import
  #               asyncio, socket or ctypes fail with ModuleNotFoundError on
  #               _socket / _ctypes.
  #   LC_ALL      the echo tests compare against English messages, so on a
  #               non-English system gettext translates them and the comparison
  #               fails on the translation rather than on any behaviour. It has
  #               to be C.UTF-8 and not plain C: the C locale is not UTF-8, and
  #               five tests which carry non-ASCII paths (the include/use tests
  #               and utf8-import) fail under it instead.
  export PATH="$ROOT/$BUILD_DIR/staging:$PATH"
  export LANG=C.UTF-8 LC_ALL=C.UTF-8
  cp -n "$ROOT/$BUILD_DIR/staging"/*.pyd "$ROOT/$BUILD_DIR/" 2>/dev/null || true

  echo "==> ctest -j$JOBS ${CTEST_ARGS[*]:-}"
  set +e
  ctest --test-dir "$BUILD_DIR" -j"$JOBS" "${CTEST_ARGS[@]:-}"
  ctest_rc=$?
  set -e

  # ipython-smoke and repl-smoke drive the REPL, which spawns pythonscad.exe as a
  # child. ctest replaces the test environment wholesale, so the PATH above does
  # not reach that child and these two cannot pass from the build tree. They do
  # pass against the staged binary, so run them there.
  echo "==> REPL smoke tests against the staged binary"
  for t in test_ipython_cli.py test_repl_cli.py; do
    if [ -f "$ROOT/tests/$t" ]; then
      printf '  %-22s ' "$t"
      if python "$(cygpath -m "$ROOT/tests/$t")" \
           "$(cygpath -m "$ROOT/$BUILD_DIR/staging/pythonscad.com")" 2>&1 |
           grep -q '^PASS'; then
        echo "PASS"
      else
        echo "FAIL"
        ctest_rc=1
      fi
    fi
  done

  [ "$ctest_rc" -eq 0 ] || exit "$ctest_rc"
fi

echo "==> done. Runnable binary: $BUILD_DIR/staging/pythonscad.exe"
