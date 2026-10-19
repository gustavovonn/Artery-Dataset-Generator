#!/usr/bin/env bash
# Runs the simulation inside the container; arguments go to opp_run (default: -c lust).
# Output: data/groundtruth/<config>-run<n>/
set -euo pipefail

CONTAINER=artery-dataset
ARGS=("$@")
[[ " ${ARGS[*]} " == *" -c "* ]] || ARGS=(-c lust "${ARGS[@]}")

docker exec -w /workspace "$CONTAINER" bash -c "
    set -e
    cmake -DRUN_FLAGS='${ARGS[*]}' build >/dev/null
    cmake --build build --target run_dataset
"
