#!/bin/zsh

set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== PRPG Build Script ===${NC}"

# Check if vcpkg exists
if [ ! -d "vcpkg" ] || [ ! -f "vcpkg/vcpkg" ]; then
    echo -e "${YELLOW}vcpkg not found. Cloning vcpkg...${NC}"
    if [ ! -d "vcpkg" ]; then
        git clone https://github.com/Microsoft/vcpkg.git
    fi
    echo -e "${GREEN}Bootstrapping vcpkg...${NC}"
    ./vcpkg/bootstrap-vcpkg.sh
else
    echo -e "${GREEN}vcpkg found.${NC}"
fi

# Install required libraries using vcpkg.json manifest mode
if [ -f "vcpkg.json" ]; then
    echo -e "${GREEN}Installing vcpkg dependencies...${NC}"
    cd vcpkg
    ./vcpkg install --x-manifest-root="$(dirname "$PWD")"
    cd -
else
    echo -e "${YELLOW}vcpkg.json not found. Skipping dependency installation.${NC}"
fi

# Configure the project
echo -e "${GREEN}Configuring project...${NC}"
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake

# Build the project
echo -e "${GREEN}Building project...${NC}"
cmake --build build -- -j"$(nproc)"

# Run the executable
echo -e "${GREEN}Running application...${NC}"
./build/prpg
