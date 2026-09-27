cmake_minimum_required(VERSION 3.28.3)

project(n-body-simulator)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# No C++20 modules here: skip dependency scanning (with Ninja and Clang it would otherwise need clang-scan-deps).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# pixelkiln_add_shaders runs Vulkan::glslc, an imported target only visible in the directory that finds it.
find_package(Vulkan REQUIRED COMPONENTS glslc)

# PixelKiln (git submodule external/PixelKiln) brings its own VulkanMemoryAllocator and GLFW submodules.
# EXCLUDE_FROM_ALL keeps its examples out of the default build.
foreach(submodule PixelKiln PixelKiln/external/VulkanMemoryAllocator)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/${submodule}/CMakeLists.txt)
        message(FATAL_ERROR "external/${submodule} is missing, run: git submodule update --init --recursive")
    endif()
endforeach()
add_subdirectory(external/PixelKiln EXCLUDE_FROM_ALL)
if(NOT TARGET glfw)
    message(FATAL_ERROR "GLFW is unavailable: run git submodule update --init --recursive, and on Linux install the "
                        "X11 development headers (Xlib, Xrandr, Xinerama, Xcursor, XInput2). Missing headers are "
                        "cached, so delete CMakeCache.txt (CLion: Tools > CMake > Reset Cache and Reload Project) "
                        "before configuring again")
endif()

# Set after PixelKiln is added so it only applies to this project's targets.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

add_executable(n-body-simulator main.cpp)

target_link_libraries(n-body-simulator PRIVATE PixelKiln glfw)

pixelkiln_add_shaders(n-body-simulator
    shaders/simulate.comp
    shaders/particle.vert
    shaders/particle.frag)
