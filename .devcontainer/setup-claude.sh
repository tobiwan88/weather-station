#!/usr/bin/env bash
# setup-claude.sh — run on postStartCommand to ensure ~/.claude is ready.
#
# The host's ~/.claude is bind-mounted into the container so credentials
# and global settings survive container restarts without re-authentication.
# This script handles first-run initialisation when the host dir is new or empty.

set -euo pipefail

CLAUDE_DIR="${HOME}/.claude"
SETTINGS="${CLAUDE_DIR}/settings.json"
MEMORY_DIR="${CLAUDE_DIR}/projects/-home-zephyr-workspace-weather-station/memory"

# 1. Ensure directory exists (first run on a host that never had Claude installed)
mkdir -p "${CLAUDE_DIR}"

# Fix ownership if Docker created the bind-mount dir as root (common on first container start).
# Fails silently when running without sudo — that's fine, it means the dir is already owned correctly.
sudo chown -R zephyr:zephyr "${CLAUDE_DIR}" 2>/dev/null || true

# 2. Initialise settings.json if absent or empty
if [ ! -s "${SETTINGS}" ]; then
    echo '{}' > "${SETTINGS}"
    echo "[setup-claude] created ${SETTINGS}"
fi

# 3. Ensure the project memory directory exists so auto-memory writes succeed
mkdir -p "${MEMORY_DIR}"

echo "[setup-claude] ~/.claude is ready"
