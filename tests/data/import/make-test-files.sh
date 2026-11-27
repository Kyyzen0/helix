#!/usr/bin/env bash
# Writes the small files the import tests read into this folder.
#
#   tests/data/import/make-test-files.sh [path of the Blender executable]
#
# The hand made files (gltf, glb, stl, ply, obj, usda, usdz) need only Python 3. The Draco
# compressed glb, the fbx and the blend need Blender, which runs in the background. The files are
# committed, so this is only run when one of them has to change.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BLENDER="${1:-/Applications/Blender.app/Contents/MacOS/Blender}"

python3 "${HERE}/make_test_files.py" "${HERE}"

if [ -x "${BLENDER}" ]; then
    "${BLENDER}" --background --factory-startup --python-exit-code 1 \
        --python "${HERE}/make_blender_files.py" -- "${HERE}"
else
    echo "Blender not found at ${BLENDER}: cube-draco.glb, cube.fbx and cube.blend were not written" >&2
fi
