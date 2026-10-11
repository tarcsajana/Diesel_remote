#!/usr/bin/env bash
set -euo pipefail

ENV="${1:-esp32c3}"
REPO="${2:-}"

if [ -z "$REPO" ]; then
  REPO="$(git remote get-url origin 2>/dev/null || true)"
  if [ -n "$REPO" ]; then
    REPO="${REPO#https://github.com/}"
    REPO="${REPO#git@github.com:}"
    REPO="${REPO%.git}"
  fi
fi

if [ -z "$REPO" ]; then
  REPO="tarcsajana/Diesel_remote"
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if command -v platformio >/dev/null 2>&1; then
  PLATFORMIO="platformio"
elif [ -x "$HOME/.platformio/penv/bin/platformio" ]; then
  PLATFORMIO="$HOME/.platformio/penv/bin/platformio"
else
  echo "PlatformIO CLI not found in PATH and not found in ~/.platformio/penv/bin" >&2
  exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "GitHub CLI (gh) not found in PATH" >&2
  exit 1
fi

echo "Building Diesel_remote for environment '$ENV'..."
"$PLATFORMIO" run -e "$ENV"

TAG="$(gh release list --repo "$REPO" --limit 1 --exclude-drafts --json tagName --jq '.[0].tagName')"
if [ -z "$TAG" ]; then
  echo "No release tag found in repository '$REPO'." >&2
  exit 1
fi

FIRMWARE_PATH=".pio/build/$ENV/firmware.bin"
if [ ! -f "$FIRMWARE_PATH" ]; then
  echo "Firmware not found at '$FIRMWARE_PATH'." >&2
  exit 1
fi

echo "Uploading firmware to release: $TAG"
gh release upload "$TAG" "$FIRMWARE_PATH#firmware.bin" --repo "$REPO" --clobber

echo "Upload completed."
