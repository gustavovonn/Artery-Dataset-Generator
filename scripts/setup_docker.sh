#!/usr/bin/env bash
# Builds the artery-dev image (once) and starts the "artery-dataset" container with this repo at /workspace.
set -euo pipefail

CONTAINER=artery-dataset
IMAGE=artery-dev
WORKSPACE="$(cd "$(dirname "$0")/.." && pwd)"
DOCKERFILE="$WORKSPACE/artery/.devcontainer/debian/Dockerfile"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build -t "$IMAGE" -f "$DOCKERFILE" "$WORKSPACE"
fi

# a container created from another checkout keeps mounting that path: recreate it
if docker ps -a --format '{{.Names}}' | grep -qx "$CONTAINER"; then
    MOUNTED=$(docker inspect "$CONTAINER" --format '{{range .Mounts}}{{if eq .Destination "/workspace"}}{{.Source}}{{end}}{{end}}')
    if [ "$MOUNTED" != "$WORKSPACE" ]; then
        echo "container $CONTAINER mounts $MOUNTED, recreating it for $WORKSPACE"
        docker rm -f "$CONTAINER" >/dev/null
    fi
fi

if docker ps -a --format '{{.Names}}' | grep -qx "$CONTAINER"; then
    docker start "$CONTAINER" >/dev/null
else
    docker run -d --name "$CONTAINER" \
        --user "$(id -u):$(id -g)" \
        --shm-size=4g \
        -v "$WORKSPACE":/workspace -w /workspace \
        "$IMAGE" sleep infinity >/dev/null
fi
echo "container $CONTAINER running, workspace $WORKSPACE -> /workspace"
