#!/usr/bin/env bash
# install-renode.sh — install Renode portable release into /opt/renode
#
# Run once in the persistent devcontainer:
#   sudo .devcontainer/install-renode.sh
#
# Survives container restarts since /opt/renode lives on the persistent overlay.
# Idempotent: skips download if already installed and version matches.

set -euo pipefail

RENODE_VERSION="1.16.1"
RENODE_INSTALL_DIR="/opt/renode"

# Detect architecture and map to Renode asset name
ARCH=$(uname -m)
case "$ARCH" in
    aarch64|arm64)
        RENODE_URL="https://github.com/renode/renode/releases/download/v${RENODE_VERSION}/renode-${RENODE_VERSION}.linux-arm64-portable-dotnet.tar.gz"
        ;;
    x86_64|amd64)
        RENODE_URL="https://github.com/renode/renode/releases/download/v${RENODE_VERSION}/renode-${RENODE_VERSION}.linux-portable-dotnet.tar.gz"
        ;;
    *)
        echo "ERROR: unsupported architecture '$ARCH'" >&2
        exit 1
        ;;
esac

# Idempotent: skip if already installed at the right version
if [ -x "${RENODE_INSTALL_DIR}/renode" ]; then
    INSTALLED=$("${RENODE_INSTALL_DIR}/renode" --version 2>/dev/null | head -1 | awk '{print $2}' | cut -d. -f1-3 || true)
    if [ "$INSTALLED" = "$RENODE_VERSION" ]; then
        echo "Renode ${RENODE_VERSION} already installed at ${RENODE_INSTALL_DIR}"
        exit 0
    fi
    echo "Renode ${INSTALLED} found, upgrading to ${RENODE_VERSION}..."
    rm -rf "${RENODE_INSTALL_DIR}"
fi

echo "Installing Renode ${RENODE_VERSION} for ${ARCH}..."

# Install .NET runtime dependency: libicu (ICU globalization library)
# Debian trixie uses libicu76, bookworm uses libicu72
if ! dpkg -s libicu76 libicu-dev >/dev/null 2>&1; then
    apt-get update -qq
    apt-get install -y -qq libicu76 libicu-dev 2>/dev/null || \
    apt-get install -y -qq libicu72 libicu-dev 2>/dev/null || \
    apt-get install -y -qq libicu-dev 2>/dev/null || true
fi

echo "Downloading Renode portable release..."
mkdir -p "${RENODE_INSTALL_DIR}"

TMP_FILE=$(mktemp)
trap 'rm -f "$TMP_FILE"' EXIT

curl -L --progress-bar "${RENODE_URL}" -o "${TMP_FILE}"

echo "Extracting to ${RENODE_INSTALL_DIR}..."
tar xzf "${TMP_FILE}" -C "${RENODE_INSTALL_DIR}" --strip-components=1

rm -f "${TMP_FILE}"

# Add to system PATH via profile.d
PROFILE_D="/etc/profile.d/renode.sh"
if [ ! -f "${PROFILE_D}" ]; then
    echo "export PATH=\"${RENODE_INSTALL_DIR}:\${PATH}\"" > "${PROFILE_D}"
    chmod 644 "${PROFILE_D}"
    echo "Added ${RENODE_INSTALL_DIR} to PATH via ${PROFILE_D}"
fi

# Install Robot Framework (version compatible with renode-test)
echo "Installing Robot Framework..."
if /home/zephyr/.venv/bin/python3 -c "import robot" 2>/dev/null; then
    /home/zephyr/.venv/bin/pip install --quiet 'robotframework==6.1'
else
    /home/zephyr/.venv/bin/pip install --quiet 'robotframework==6.1'
fi

# Verify
echo -n "Renode version: "
"${RENODE_INSTALL_DIR}/renode" --version | head -1
echo -n "Robot Framework version: "
/home/zephyr/.venv/bin/python3 -c "import robot; print(robot.version.VERSION)"
echo "Renode installed successfully."
