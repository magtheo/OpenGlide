#!/usr/bin/env bash
# Unit-test main.qml's text-model logic without Qt, a display, or the decoder.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
NODE="$(command -v node || command -v nodejs || true)"
[[ -x "$NODE" ]] || { echo "needs node (any recent version)" >&2; exit 1; }
python3 extract.py ../qt-prototype/main.qml /tmp/openglide_extracted.js
"$NODE" history_test.js /tmp/openglide_extracted.js
echo
exec "$NODE" state_test.js /tmp/openglide_extracted.js
