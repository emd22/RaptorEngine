

cmake_minimum_required(VERSION 3.26)

project(ThirdPartyLib)

# add third party sources to static library
file (GLOB_RECURSE THIRD_PARTY_SOURCES "ThirdParty/*.cpp" "ThirdParty/*.h" "ThirdParty/*.hpp")
add_library(ThirdPartyLib STATIC ${THIRD_PARTY_SOURCES})
target_compile_options(ThirdPartyLib PRIVATE -Wno-nullability-extension)


project(cr CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

file (GLOB_RECURSE SOURCES "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "ThirdParty/.*")

file (GLOB_RECURSE HEADERS "*.hpp")
list(FILTER HEADERS EXCLUDE REGEX "ThirdParty/.*")


find_package(Vulkan REQUIRED)
find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
find_package(JPEG REQUIRED)

add_definitions(
    -DFX_USE_NEON
    -DFX_DEBUG_STATIC_ARRAY


)

add_compile_options(
    -g
    -Wall
    # -Wpedantic
    -fsanitize=address
    -Wno-nullability-extension
)


add_link_options(
    -fsanitize=address
)

include_directories(
    ./
    ${Vulkan_INCLUDE_DIRS}
    JPEG::JPEG
    # ${SDL3_INCLUDE_DIRS}
)

add_executable(cr ${SOURCES} ${HEADERS})

target_link_libraries(cr ThirdPartyLib ${Vulkan_LIBRARIES} ${SDL3_LIBRARIES} JPEG::JPEG)
