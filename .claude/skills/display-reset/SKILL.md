---
name: display-reset
description: Kill and restart the Xvfb + x11vnc display stack when the framebuffer has hung or VNC is unresponsive.
disable-model-invocation: true
allowed-tools: Bash
---

# Display Reset

Use when: VNC shows a frozen/black screen, `zephyr.exe` exits with display errors, or `start-display.sh` reports Xvfb is already running but VNC is inaccessible.

## Reset procedure

```bash
pkill -9 Xvfb x11vnc 2>/dev/null || true && bash .devcontainer/start-display.sh
```

Run from the workspace root. The script sets `DISPLAY=:1` and starts x11vnc on port 5900 (password `zephyr`).

## Verify

```bash
DISPLAY=:1 xdpyinfo | head -5
```

Expected: X protocol and screen dimensions. If this fails, the devcontainer may need a full restart.

## Do NOT

- Do not prefix `DISPLAY=:1` before running `zephyr.exe` manually — it is pre-set in `devcontainer.json`.
- Do not use `-uart_stdinout`; connect to the binary via pseudotty (`/dev/pts/N`).
