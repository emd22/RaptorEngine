cmake_minimum_required(VERSION 3.16)
project(foxtrot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo" CACHE STRING "" FORCE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/${CMAKE_BUILD_TYPE}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/${CMAKE_BUILD_TYPE}")

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

set(VS_STARTUP_PROJECT foxtrot)

option(USE_SIMDE "Use SIMDe to use AVX on a non-AVX platform" OFF)

file(GLOB_RECURSE SOURCES
    "Src/*.hpp" "Src/*.inl" "Src/*.cpp"
    "Src/ThirdParty/*.cpp" "Src/ThirdParty/*.h"
    "Src/ThirdParty/*.hpp" "Src/ThirdParty/*.inl"
)

#file(GLOB SHADER_SOURCES
#    "Shaders/*.hlsl"
#)

message(STATUS "${SHADER_SOURCES}")

#set_source_files_properties(${SHADER_SOURCES} PROPERTIES LANGUAGE HLSL)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES})


# Add shaders as a Visual Studio filter, borrowed from Hyperion Engine
file(GLOB_RECURSE SHADER_FILES "${CMAKE_SOURCE_DIR}/Shaders/*")
source_group("Shaders" FILES ${SHADER_FILES})

# we don't want to build shader files to be compiled by fxc but we want them in the solution
foreach(SHADER_FILE ${SHADER_FILES})
    if(MSVC)
        set_property(SOURCE ${SHADER_FILE} PROPERTY VS_TOOL_OVERRIDE "None")
    endif()

    list(APPEND SOURCES ${SHADER_FILE})
endforeach()

add_executable(foxtrot ${SOURCES})

if(WIN32)
    target_compile_options(foxtrot PRIVATE /Oi /MP /arch:AVX2)
    target_link_directories(foxtrot PRIVATE "./Lib/Win32")

    if(DEFINED ENV{VULKAN_SDK})
        target_include_directories(foxtrot SYSTEM PRIVATE "$ENV{VULKAN_SDK}/include")
        target_link_directories(foxtrot PRIVATE "$ENV{VULKAN_SDK}/Lib")
    endif()

    target_link_libraries(foxtrot PRIVATE vulkan-1 dxcompiler)
    target_compile_definitions(foxtrot PRIVATE _USE_MATH_DEFINES)

elseif(APPLE)
    set_target_properties(foxtrot PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "aarch64")
    target_compile_options(foxtrot PRIVATE -Wno-nullability-completeness)
    target_link_directories(foxtrot PRIVATE "./Lib/macOS")

    set(VK_SDK $ENV{VULKAN_SDK})
    target_include_directories(foxtrot SYSTEM BEFORE PRIVATE "${VK_SDK}/include")
    target_link_directories(foxtrot PRIVATE "${VK_SDK}/lib")
    set_target_properties(foxtrot PROPERTIES BUILD_RPATH "${VK_SDK}/lib")

    target_link_libraries(foxtrot PRIVATE vulkan.1 dxcompiler)
    target_compile_definitions(foxtrot PRIVATE VK_ENABLE_BETA_EXTENSIONS=1)
endif()

target_include_directories(foxtrot PRIVATE
    "Src"
    "Src/ThirdParty"
    "Lib/Include"
)

target_compile_definitions(foxtrot PRIVATE
    FX_BASE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(foxtrot PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:RelWithDebInfo>:NDEBUG>
)

if(USE_SIMDE)
    target_compile_definitions(foxtrot PRIVATE FX_USE_SIMDE SIMDE_ENABLE_NATIVE_ALIASES)
endif()


if(MSVC)
    target_compile_options(foxtrot PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/Ob2>
    )
else()
    target_compile_options(foxtrot PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    )
endif()

target_link_libraries(foxtrot PRIVATE turbojpeg SDL3)
