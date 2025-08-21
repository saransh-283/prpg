#!/bin/zsh
# Script to set up vcpkg if not already installed, and install required libraries
set -e

VCPKG_DIR="$(pwd)/vcpkg"
VCPKG_REPO="https://github.com/microsoft/vcpkg.git"
VCPKG_BIN="$VCPKG_DIR/vcpkg"

# Check if vcpkg is present
if [ ! -d "$VCPKG_DIR" ] || [ ! -f "$VCPKG_BIN" ]; then
    echo "Setting up vcpkg in $VCPKG_DIR..."
    if [ ! -d "$VCPKG_DIR" ]; then
        git clone "$VCPKG_REPO" "$VCPKG_DIR"
    fi
    cd "$VCPKG_DIR"
    ./bootstrap-vcpkg.sh
    cd -
else
    echo "vcpkg is already set up at $VCPKG_DIR."
fi

# Install required libraries using vcpkg.json manifest mode
if [ -f "vcpkg.json" ]; then
    cd "$VCPKG_DIR"
    ./vcpkg install --x-manifest-root="$(dirname "$VCPKG_DIR")"
    cd -
else
    echo "vcpkg.json not found in project root. Skipping dependency installation."
fi