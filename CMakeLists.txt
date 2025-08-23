cmake_minimum_required(VERSION 3.15)

set(PROJECT_NAME prpg)
project(${PROJECT_NAME})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg toolchain
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()


# Executable
add_executable(${PROJECT_NAME}
    src/main.cpp
)

# Perlin Roads C++ port
add_executable(perlin_roads src/perlin_roads.cpp)

find_package(unofficial-noise CONFIG REQUIRED)

target_link_libraries(perlin_roads PRIVATE unofficial::noise::noise-static)
target_include_directories(perlin_roads PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Add vcpkg include/lib dirs
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Find llama and its dependencies via vcpkg CMake config
find_package(llama CONFIG REQUIRED)
find_package(ggml CONFIG QUIET)
find_package(gguf CONFIG QUIET)

target_link_libraries(${PROJECT_NAME} PRIVATE llama)
if(TARGET ggml)
    target_link_libraries(${PROJECT_NAME} PRIVATE ggml)
endif()
if(TARGET gguf)
    target_link_libraries(${PROJECT_NAME} PRIVATE gguf)
endif()

# Precompiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)

file(COPY ${CMAKE_SOURCE_DIR}/src/models DESTINATION ${CMAKE_BINARY_DIR})