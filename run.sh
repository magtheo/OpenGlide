#!/usr/bin/env bash
# OpenGlide — build + launch the Qt on-screen keyboard in one command.
#
# Idempotent: configures once (when build/ is missing), then incremental-builds
# every run (~15s when sources changed, near-instant when nothing did), then
# launches. `--no-build` skips the build and launches the current binary.
#
# Forces the xcb (XWayland) platform: the bundled QML modules are tested there,
# not under the native Wayland plugin.
#
# IBus: the app self-activates the OpenGlide IME for the session (so glide
# decodes commit via IBus, UTF-8 / layout-independent) and restores your previous
# engine (e.g. vocalinux) on exit. Physical typing still works (the engine
# forwards keys).
#
# Requires the futo-spike venv (tools/futo-spike/.venv): its python drives
# ExecuTorch codegen and its cmake (4.x) is new enough for the ExecuTorch build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROTO="$ROOT/tools/qt-prototype"
VENV="$ROOT/tools/futo-spike/.venv"
CMAKE="$VENV/bin/cmake"
PYTHON="$VENV/bin/python"

if [[ ! -x "$CMAKE" ]]; then
    echo "venv cmake not found at $CMAKE" >&2
    echo "  create it first:  python3 -m venv \"$VENV\"  &&  \"$VENV/bin/pip\" install cmake" >&2
    exit 1
fi

DO_BUILD=1
case "${1:-}" in
    --no-build) DO_BUILD=0; shift;;
    -h|--help)
        echo "usage: ./run.sh [--no-build] [args passed to openglide-qt]"
        exit 0;;
esac

cd "$PROTO"
if [[ "$DO_BUILD" == 1 ]]; then
    if [[ ! -f build/CMakeCache.txt ]]; then
        echo "==> configuring (first run)..."
        "$CMAKE" -B build -S . \
            -DCMAKE_BUILD_TYPE=Release \
            -DPYTHON_EXECUTABLE="$PYTHON"
    fi
    echo "==> building..."
    "$CMAKE" --build build -j"$(nproc)"
fi

BIN="$PROTO/build/openglide-qt"
if [[ ! -x "$BIN" ]]; then
    echo "no binary at $BIN — run again without --no-build" >&2
    exit 1
fi

: "${DISPLAY:=:0}"
: "${XDG_RUNTIME_DIR:=/run/user/$(id -u)}"
export DISPLAY XDG_RUNTIME_DIR QT_QPA_PLATFORM=xcb
echo "==> launching openglide-qt..."
exec "$BIN" "$@"
