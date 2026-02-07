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
# Collect all source files under src so new modules (objects/, utils/, etc.) are included automatically
file(GLOB_RECURSE SRC_FILES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.c
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# std::thread requires pthreads on most Linux toolchains
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# Perlin Roads C++ port
find_package(unofficial-noise CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE unofficial::noise::noise-static)

# Find SDL2 and SDL2_ttf
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)

# Link SDL2
target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2 SDL2::SDL2main)

# Link SDL2_ttf (handle naming differences)
if(TARGET SDL2_ttf::SDL2_ttf)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2_ttf::SDL2_ttf)
elseif(TARGET SDL2_ttf::SDL2_ttf-static)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2_ttf::SDL2_ttf-static)
else()
    message(FATAL_ERROR "Could not find SDL2_ttf target")
endif()

# Add vcpkg include/lib dirs explicitly (not always needed)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include
    ${CMAKE_SOURCE_DIR}/src/assets
)
target_link_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib
)

# Compile definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Find llama and its dependencies
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

# Copy assets into build dir
file(COPY ${CMAKE_SOURCE_DIR}/src/assets DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/src/utils DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/src/entities/npc DESTINATION ${CMAKE_BINARY_DIR}/src/entities)

# Graphics and utility dependencies used by the procgen main
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL)
if(TARGET glad::glad)
    target_link_libraries(${PROJECT_NAME} PRIVATE glad::glad)
endif()

if(TARGET glm::glm)
    target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm)
endif()

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
endif()
