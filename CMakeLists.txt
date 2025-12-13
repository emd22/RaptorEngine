

cmake_minimum_required(VERSION 3.23)

project(ThirdPartyLib)

set(CMAKE_TOOLCHAIN_FILE, "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

if (WIN32)
message("${CMAKE_SOURCE_DIR}")
link_directories(${CMAKE_SOURCE_DIR}/Lib/win32)
include_directories(${CMAKE_SOURCE_DIR}/Lib/win32/Include)
else()
link_directories(${CMAKE_SOURCE_DIR}/Lib/macos)
endif()


# add third party sources to static library
file (GLOB_RECURSE THIRD_PARTY_SOURCES "Src/ThirdParty/*.cpp" "Src/ThirdParty/*.h" "Src/ThirdParty/*.hpp" "Src/ThirdParty/*.inl")
add_library(ThirdPartyLib STATIC ${THIRD_PARTY_SOURCES})
target_include_directories(ThirdPartyLib PRIVATE ${CMAKE_SOURCE_DIR}/Src/ThirdParty)

target_link_libraries(ThirdPartyLib slang)

target_compile_definitions(
    ThirdPartyLib
    PRIVATE
    -DVMA_RECORDING_ENABLED=1
    -DVMA_DEBUG_ALWAYS_DETECT_LEAKS=1
)
target_compile_options(ThirdPartyLib PRIVATE)


project(cr CXX)

set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

file (GLOB_RECURSE SOURCES "Src/*.cpp" "Src/*.inl")
list(FILTER SOURCES EXCLUDE REGEX "Src/ThirdParty/.*")

file (GLOB_RECURSE HEADERS "Src/*.hpp" "Src/*.inl")
list(FILTER HEADERS EXCLUDE REGEX "Src/ThirdParty/.*")

find_package(Vulkan REQUIRED)

if (APPLE)
find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
find_package(libjpeg-turbo CONFIG REQUIRED)
endif()


add_definitions(
    -DVMA_RECORDING_ENABLED=1
    -DVMA_DEBUG_ALWAYS_DETECT_LEAKS=1
	
	-D_USE_MATH_DEFINES
    # -DFX_DEBUG_STATIC_ARRAY
)

if (APPLE)
add_compile_options(
    -g
    -Wall
    # -Wpedantic
    -fsanitize=address
    -Wno-nullability-extension
    # -O2
)

add_link_options(
    -fsanitize=address
)

endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/Src
	${CMAKE_SOURCE_DIR}/Src/ThirdParty
    ${Vulkan_INCLUDE_DIRS}
    # ${SDL3_INCLUDE_DIRS}
)

add_executable(cr ${SOURCES} ${HEADERS})

target_link_libraries(cr ThirdPartyLib ${Vulkan_LIBRARIES} ${SDL3_LIBRARIES} ${JPEG_LIBRARIES})
