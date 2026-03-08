#!/usr/bin/env bash
set -euo pipefail

DISPLAY_NUM=:1
VNC_PORT=5900
# Slightly smaller geometry is often better for emulated Zephyr screens
SCREEN_GEOMETRY="800x600x24"

# --- Xvfb ---
if pgrep -x Xvfb > /dev/null 2>&1; then
    echo "[start-display] Xvfb already running"
else
    echo "[start-display] Starting Xvfb on ${DISPLAY_NUM}"
    # Added -screen 0 800x600x24 to match typical Zephyr simulation sizes
    Xvfb "${DISPLAY_NUM}" -screen 0 "${SCREEN_GEOMETRY}" -ac +extension GLX +render -noreset &

    for i in $(seq 1 10); do
        [ -S "/tmp/.X11-unix/X${DISPLAY_NUM#:}" ] && break
        sleep 0.5
    done
fi

# --- x11vnc ---
if pgrep -x x11vnc > /dev/null 2>&1; then
    echo "[start-display] x11vnc already running"
else
    echo "[start-display] Starting x11vnc on port ${VNC_PORT}"
    # CHANGES: Added -passwd zephyr and -listen 0.0.0.0
    x11vnc -display "${DISPLAY_NUM}" \
           -rfbport "${VNC_PORT}" \
           -forever \
           -passwd zephyr \
           -listen 0.0.0.0 \
           -noxdamage \
           -bg
fi

echo "[start-display] Virtual display ready — connect VNC to localhost:${VNC_PORT} (pw: zephyr)"
