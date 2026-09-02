#!/bin/bash
# Restore what a fresh (or reclaimed) web container needs to build PythonSCAD.
#
# Three things go missing and none of them is obvious from the error cmake
# gives: the submodules, the system packages, and the configured build
# directory. This installs all three, and deliberately does *not* build - a
# cold build is around an hour and 272 targets, which is not something to put
# in front of a session starting.
#
# See CLAUDE.md, "Headless build in a container or agent sandbox", for the
# reasoning behind each step.
set -euo pipefail

# Local machines have their own toolchains; this is only for the web.
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

cd "${CLAUDE_PROJECT_DIR:-$(dirname "$0")/../..}"

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  SUDO="sudo"
fi

# 1. Submodules. A fresh clone has none, and cmake reports them one failure at
#    a time - "Unknown CMake command add_sanitizers", then "MCAD not found" -
#    so do them all at once.
git submodule update --init --recursive

# 2. Dependencies, minus Qt. Ask the project for the list rather than keeping a
#    copy here that drifts: get-dependencies.py prints three header lines and
#    then one package per line, and the qt entries are the ones a HEADLESS
#    build has no use for. gettext is not optional even headless - without
#    msgfmt the build fails *after* linking pythonscad, at the locale step,
#    which looks like a build failure and is not.
PACKAGES=$(python3 ./scripts/get-dependencies.py --distro ubuntu --profile pythonscad-qt5 --list \
  | grep -E '^[a-z0-9][a-z0-9.+-]*$' \
  | grep -v qt \
  | tr '\n' ' ')
if [ -z "$PACKAGES" ]; then
  echo "session-start: could not read the dependency list from get-dependencies.py" >&2
  exit 1
fi
# apt indexes in a reclaimed container can be stale enough to 404 on real
# packages, so refresh before installing rather than after the first failure.
$SUDO apt-get update -qq
# shellcheck disable=SC2086
DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y --no-install-recommends $PACKAGES

# 3. OpenCASCADE, for tests/steproundtrip.py. Optional by design - the round
#    trip skips silently when it is absent - so a failure here must not take
#    the session down with it. It is what reads every STEP export back with a
#    real CAD kernel, which is the one thing the exporter's own validator
#    cannot do.
pip install --quiet cadquery-ocp || \
  echo "session-start: cadquery-ocp unavailable, the STEP round trip will skip" >&2

# 4. Configure, if nothing is configured yet. OPENSCAD_VERSION is required: the
#    version comes from git tags, and a shallow or tagless clone otherwise
#    fails with "Version string 'abc1234' doesn't match expected format".
#
#    An existing build directory is left alone. Re-running cmake underneath a
#    build that is in flight rewrites build.ninja while ninja is reading it.
if [ ! -f build/build.ninja ]; then
  cmake -B build -G Ninja \
    -DHEADLESS=ON \
    -DENABLE_PYTHON=ON \
    -DENABLE_TESTS=ON \
    -DEXPERIMENTAL=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENSCAD_VERSION="$(date +%Y.%m.%d)"
  echo "session-start: configured build/ - run 'cmake --build build -j3' (about an hour cold)"
else
  echo "session-start: build/ already configured, left as it is"
fi

# Keep the job count below the core count: memory is the binding constraint,
# not cores, and a CGAL translation unit peaks around 2 GB.
echo 'export CMAKE_BUILD_PARALLEL_LEVEL=3' >> "${CLAUDE_ENV_FILE:-/dev/null}"
