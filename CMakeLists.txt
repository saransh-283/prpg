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
find_package(unofficial-noise CONFIG REQUIRED)

# Build perlin_roads and llm as part of the main executable (no separate target)
target_sources(${PROJECT_NAME} PRIVATE src/perlin_roads.cpp src/llm.cpp src/llm.h)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE unofficial::noise::noise-static)

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

# Add the LLM implementation and perlin source to the main executable (no separate library)
target_sources(${PROJECT_NAME} PRIVATE src/perlin_roads.cpp src/llm.cpp)


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
file(COPY ${CMAKE_SOURCE_DIR}/src/assets DESTINATION ${CMAKE_BINARY_DIR})