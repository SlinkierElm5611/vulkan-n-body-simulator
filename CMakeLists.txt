cmake_minimum_required(VERSION 3.15)

project(n-body-simulator)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

add_custom_target(shaders
    COMMAND glslc -fshader-stage=vertex -o vert.spv vert.glsl
    COMMAND glslc -fshader-stage=fragment -o frag.spv frag.glsl
    COMMAND glslc -fshader-stage=compute -o comp.spv comp.glsl
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS vert.glsl frag.glsl comp.glsl
    BYPRODUCTS vert.spv frag.spv comp.spv
)

set(SHADER_HEADERS vert.h frag.h comp.h)

add_custom_target(shaders_headers
    COMMAND xxd -i vert.spv > vert.h
    COMMAND xxd -i frag.spv > frag.h
    COMMAND xxd -i comp.spv > comp.h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS shaders
    BYPRODUCTS vert.h frag.h comp.h
)

find_package(Vulkan REQUIRED)

add_subdirectory(VulkanMemoryAllocator)

add_subdirectory(GLFW)

add_executable(n-body-simulator main.cpp ${SHADER_HEADERS})

add_dependencies(n-body-simulator glfw Vulkan::Vulkan shaders_headers)

target_link_libraries(n-body-simulator glfw Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
