#!/usr/bin/env bash
# Configures (first time) and builds Artery + the dataset feature inside the container.
set -euo pipefail

CONTAINER=artery-dataset
BUILD_TYPE=${BUILD_TYPE:-Release}

docker exec -w /workspace "$CONTAINER" bash -c "
    set -e
    [ -f build/CMakeCache.txt ] || cmake -B build -DCMAKE_BUILD_TYPE=$BUILD_TYPE .
    cmake --build build -j\$(nproc)
"
