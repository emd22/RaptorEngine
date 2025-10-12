

cmake_minimum_required(VERSION 3.26)

project(ThirdPartyLib)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

link_directories(../Lib)
include_directories(ThirdParty)

# add third party sources to static library
file (GLOB_RECURSE THIRD_PARTY_SOURCES "ThirdParty/*.cpp" "ThirdParty/*.h" "ThirdParty/*.hpp" "ThirdParty/*.inl")
add_library(ThirdPartyLib STATIC ${THIRD_PARTY_SOURCES})
target_link_libraries(ThirdPartyLib slang)

target_compile_definitions(
    ThirdPartyLib
    PRIVATE
    -DVMA_RECORDING_ENABLED=1
    -DVMA_DEBUG_ALWAYS_DETECT_LEAKS=1
)
target_compile_options(ThirdPartyLib PRIVATE -Wno-nullability-extension -Wno-nullability-completeness)


project(cr CXX)

set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

file (GLOB_RECURSE SOURCES "*.cpp" "*.inl")
list(FILTER SOURCES EXCLUDE REGEX "ThirdParty/.*")

file (GLOB_RECURSE HEADERS "*.hpp" "*.inl")
list(FILTER HEADERS EXCLUDE REGEX "ThirdParty/.*")

find_package(Vulkan REQUIRED)
find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
find_package(JPEG REQUIRED)

add_definitions(
    -DFX_USE_NEON
    -DVMA_RECORDING_ENABLED=1
    -DVMA_DEBUG_ALWAYS_DETECT_LEAKS=1
    # -DFX_DEBUG_STATIC_ARRAY
)

add_compile_options(
    -g
    -Wall
    # -Wpedantic
    # -fsanitize=address
    -Wno-nullability-extension
    # -O2
)


add_link_options(
    # -fsanitize=address
)

include_directories(
    ../Src
    ${Vulkan_INCLUDE_DIRS}
    JPEG::JPEG
    # ${SDL3_INCLUDE_DIRS}
)

add_executable(cr ${SOURCES} ${HEADERS})

target_link_libraries(cr ThirdPartyLib ${Vulkan_LIBRARIES} ${SDL3_LIBRARIES} JPEG::JPEG)
