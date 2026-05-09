#!/usr/bin/env bash
# Generate a local ED25519 signing key for MCUboot.
# The private key is stored in keys/ which is gitignored — never commit it.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
KEY_FILE="${REPO_ROOT}/keys/dev-ed25519.pem"

mkdir -p "${REPO_ROOT}/keys"

if [ -f "${KEY_FILE}" ]; then
    echo "Key already exists at ${KEY_FILE} — skipping generation."
    exit 0
fi

imgtool keygen -k "${KEY_FILE}" -t ed25519
chmod 600 "${KEY_FILE}"
echo "Dev key generated: ${KEY_FILE}"
echo "This file is gitignored. Never commit it."
