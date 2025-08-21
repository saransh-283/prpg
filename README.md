# prpg

## Setup Instructions

### 1. Install vcpkg and Dependencies

Run the following command in the project root to set up vcpkg and install all dependencies specified in `vcpkg.json`:

```sh
./setup_vcpkg.sh
```

This script will:
- Clone and bootstrap vcpkg if not already present in the `vcpkg` directory.
- Install all dependencies listed in `vcpkg.json` (e.g., opengl, sdl2, nlohmann-json).

### 2. Build the Project

Use CMake to configure and build the project:

```sh
cmake -B build -S .
cmake --build build
```

If you want to use the vcpkg toolchain explicitly, you can run:

```sh
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### 3. Add/Remove Dependencies

Edit `vcpkg.json` to add or remove dependencies. Then re-run `./setup_vcpkg.sh` to install any new dependencies.

---

For more information about vcpkg, see: https://github.com/microsoft/vcpkg
