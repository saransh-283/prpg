#!/bin/zsh
# Build script: installs vcpkg packages and builds the project
set -e

# Install vcpkg packages
./setup_vcpkg.sh

# Configure and build with CMake
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
