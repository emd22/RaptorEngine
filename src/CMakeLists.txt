cmake_minimum_required(VERSION 3.26)
project(cr CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

file (GLOB_RECURSE SOURCES "*.cpp")
file (GLOB_RECURSE HEADERS "*.hpp" "ThirdParty/*.h")

set(COMPILE_FLAGS "-g -Wall")

find_package(Vulkan REQUIRED)
find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)

add_definitions(
    -DFX_USE_NEON
    # -DFX_DEBUG_STATIC_ARRAY
    ${COMPILE_FLAGS}
)
include_directories(
    ./
    ${Vulkan_INCLUDE_DIRS}
    # ${SDL3_INCLUDE_DIRS}
)

add_executable(cr ${SOURCES} ${HEADERS})

target_link_libraries(cr ${Vulkan_LIBRARIES} ${SDL3_LIBRARIES})
