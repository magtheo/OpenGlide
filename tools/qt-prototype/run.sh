#!/usr/bin/env bash
# Launch the OpenGlide Qt prototype.
#
# Forces the xcb (XWayland) platform — the bundled QML modules are tested there,
# not under the native Wayland plugin.
#
# IBus note: the app self-activates the OpenGlide IME for the duration of the
# session (so glide decodes commit via IBus, UTF-8 / layout-independent) and
# restores your previous engine (e.g. vocalinux) when it exits. Physical typing
# still works while it's active (the engine forwards keys).
set -euo pipefail
cd "$(dirname "$0")"

if [[ ! -x ./build/openglide-qt ]]; then
    echo "build/openglide-qt not found — build first:" >&2
    echo "  ../../tools/futo-spike/.venv/bin/cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DPYTHON_EXECUTABLE=\$PWD/../futo-spike/.venv/bin/python" >&2
    echo "  ../../tools/futo-spike/.venv/bin/cmake --build build -j4" >&2
    exit 1
fi

: "${DISPLAY:=:0}"
: "${XDG_RUNTIME_DIR:=/run/user/$(id -u)}"
export DISPLAY XDG_RUNTIME_DIR QT_QPA_PLATFORM=xcb
exec ./build/openglide-qt "$@"
