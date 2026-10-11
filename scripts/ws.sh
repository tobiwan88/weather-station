#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Run a command in the Zephyr build environment, from the west workspace root.
#
#   scripts/ws.sh 'west build -b native_sim/native/64 weather-station/apps/gateway'
#
# Inside the devcontainer / CI image the command runs directly. On a macOS host
# it is forwarded into a running container (Apple `container` or Docker) named
# $WS_CONTAINER (default: ws_build) that mounts the west workspace at
# /home/zephyr/workspace.
set -euo pipefail

WS_ROOT=/home/zephyr/workspace
WS_CONTAINER=${WS_CONTAINER:-ws_build}
cmd="source /home/zephyr/.venv/bin/activate; export ZEPHYR_BASE=$WS_ROOT/zephyr; cd $WS_ROOT; $*"

if [ -d "$WS_ROOT/zephyr" ]; then
	exec bash -c "$cmd"
elif command -v container >/dev/null 2>&1; then
	exec container exec "$WS_CONTAINER" bash -lc "$cmd"
elif command -v docker >/dev/null 2>&1; then
	exec docker exec "$WS_CONTAINER" bash -lc "$cmd"
fi

echo "ws.sh: no build environment found (start the '$WS_CONTAINER' container)" >&2
exit 1
