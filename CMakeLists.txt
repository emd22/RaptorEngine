cmake_minimum_required(VERSION 4.0)
project(raptor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo" CACHE STRING "" FORCE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/${CMAKE_BUILD_TYPE}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/${CMAKE_BUILD_TYPE}")

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

option(USE_SIMDE "Use SIMDe to use AVX on a non-AVX platform" OFF)
option(USE_MOLTENVK "Compile with support for MoltenVK. Defaults to KosmicKrisp on macOS" OFF)

# Fallback directory the engine searches for assets (Data/, Config/, Shaders/) when they aren't
# found next to the executable (see FilesystemIO::ResolvePath). Defaults to "." (resolved against
# the working directory at runtime); CMakePresets.json overrides this to the absolute source dir
# so the fallback works regardless of what directory the executable is launched from.
set(FX_BASE_DIR "." CACHE PATH "Fallback base directory for engine assets")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "Src/*.hpp" "Src/*.inl" "Src/*.cpp"
    "Src/ThirdParty/*.cpp" "Src/ThirdParty/*.h"
    "Src/ThirdParty/*.hpp" "Src/ThirdParty/*.inl"
)

#file(GLOB SHADER_SOURCES
#    "Shaders/*.hlsl"
#)

message(STATUS "${SHADER_SOURCES}")

#set_source_files_properties(${SHADER_SOURCES} PROPERTIES LANGUAGE HLSL)

list(FILTER SOURCES EXCLUDE REGEX "/Src/ThirdParty/Jolt/")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES})

set(RX_RUST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SrcRS")

# The Rust workspace is the entry point and builds this project as a static library (see SrcRS/raptor-app/build.rs), so
# the C++ only needs the generated C header from raptor-ffi
add_library(raptor_ffi INTERFACE)
target_include_directories(raptor_ffi INTERFACE "${RX_RUST_DIR}/raptor-ffi/include")


# Add shaders as a Visual Studio filter, borrowed from Hyperion Engine
file(GLOB_RECURSE SHADER_FILES "${CMAKE_SOURCE_DIR}/Shaders/*")
source_group("Shaders" FILES ${SHADER_FILES})

# we don't want to build shader files to be compiled by c but we want them in the solution
foreach(SHADER_FILE ${SHADER_FILES})
    if(MSVC)
        set_property(SOURCE ${SHADER_FILE} PROPERTY VS_TOOL_OVERRIDE "None")
    endif()

    list(APPEND SOURCES ${SHADER_FILE})
endforeach()

add_library(raptor_cpp STATIC ${SOURCES})

if(WIN32)
    target_compile_options(raptor_cpp PRIVATE /Oi /MP /arch:AVX2)
    target_link_directories(raptor_cpp PRIVATE "./Lib/Win32")

    if(DEFINED ENV{VULKAN_SDK})
        target_include_directories(raptor_cpp SYSTEM PRIVATE "$ENV{VULKAN_SDK}/include")
        target_link_directories(raptor_cpp PRIVATE "$ENV{VULKAN_SDK}/Lib")
    endif()

    target_link_libraries(raptor_cpp PRIVATE vulkan-1)
    target_compile_definitions(raptor_cpp PRIVATE _USE_MATH_DEFINES)

    # When FX_BASE_DIR is left as the default "." it resolves against the working directory, and
    # assets (Shaders/, Data/, Config/) live at the repo root, not next to the generated .vcxproj.
    # Without this, VS defaults the debugger's working directory to $(ProjectDir) (the build
    # folder) and asset loading fails.
    set_target_properties(raptor_cpp PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # These are only shipped as DLLs (no import-time search path covers Lib/win32), so copy them
    # next to the executable or the debugger (and any run outside this directory) can't find them.
    add_custom_command(TARGET raptor_cpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/Lib/win32/SDL3.dll"
            "${CMAKE_SOURCE_DIR}/Lib/win32/zstd.dll"
            "$<TARGET_FILE_DIR:raptor_cpp>"
    )

elseif(APPLE)
    set_target_properties(raptor_cpp PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "aarch64")

    target_compile_options(raptor_cpp PRIVATE
        -Wno-nullability-completeness
        -Wcast-align
        #-Wold-style-cast
        -Wpessimizing-move
        -Wredundant-move
        -Wshadow
        #-Wdouble-promotion
    )
    target_link_directories(raptor_cpp PRIVATE "./Lib/macOS")

    set(VK_SDK $ENV{VULKAN_SDK})
    target_include_directories(raptor_cpp SYSTEM BEFORE PRIVATE "${VK_SDK}/include")
    target_link_directories(raptor_cpp PRIVATE "${VK_SDK}/lib")
    set_target_properties(raptor_cpp PROPERTIES BUILD_RPATH "${VK_SDK}/lib")

    target_link_libraries(raptor_cpp PRIVATE vulkan.1)
    target_compile_definitions(raptor_cpp PRIVATE VK_ENABLE_BETA_EXTENSIONS=1)

	target_compile_options(raptor_cpp PRIVATE  -fcolor-diagnostics)
	target_link_options(raptor_cpp PRIVATE )
endif()


target_include_directories(raptor_cpp PRIVATE
    "Src"
    "Src/ThirdParty"
    "Lib/Include"
)

target_compile_definitions(raptor_cpp PRIVATE
    FX_BASE_DIR="${FX_BASE_DIR}"
)

target_compile_definitions(raptor_cpp PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:RelWithDebInfo>:NDEBUG>
)



if(USE_SIMDE)
    target_compile_definitions(raptor_cpp PRIVATE FX_USE_SIMDE SIMDE_ENABLE_NATIVE_ALIASES)
endif()

if(USE_MOLTENVK)
    target_compile_definitions(raptor_cpp PRIVATE FX_USE_PORTABILITY_EXTENSION)
endif()

if(MSVC)
    target_compile_options(raptor_cpp PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/Ob2>
    )
else()
    target_compile_options(raptor_cpp PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    )
endif()

target_link_libraries(raptor_cpp PRIVATE raptor_ffi)


target_link_libraries(raptor_cpp PRIVATE SDL3)


# Describes how to link raptor_cpp into the Rust executable (SrcRS/raptor-app/build.rs reads it). CMake only knows the
# full link line while it is building an executable, so walk the target graph and write the libraries out.
set_property(GLOBAL PROPERTY RX_LINK_ITEMS "")
set_property(GLOBAL PROPERTY RX_LINK_SEEN "")
set_property(GLOBAL PROPERTY RX_LINK_TARGETS_SEEN "")

function(rx_add_link_item item)
    if(NOT item MATCHES "^-")
        get_property(seen GLOBAL PROPERTY RX_LINK_SEEN)
        if(item IN_LIST seen)
            return()
        endif()
        set_property(GLOBAL APPEND PROPERTY RX_LINK_SEEN "${item}")
    endif()
    set_property(GLOBAL APPEND PROPERTY RX_LINK_ITEMS "${item}")
endfunction()

function(rx_walk_link_libs libs)
    foreach(lib IN LISTS libs)
        if(lib MATCHES "^\\$<LINK_ONLY:(.*)>$")
            set(lib "${CMAKE_MATCH_1}")
        endif()

        if(lib STREQUAL "")
            continue()
        endif()

        if(TARGET "${lib}")
            get_property(seen GLOBAL PROPERTY RX_LINK_TARGETS_SEEN)
            if(lib IN_LIST seen)
                continue()
            endif()
            set_property(GLOBAL APPEND PROPERTY RX_LINK_TARGETS_SEEN "${lib}")

            get_target_property(type "${lib}" TYPE)
            if(type STREQUAL "STATIC_LIBRARY" OR type STREQUAL "SHARED_LIBRARY" OR type STREQUAL "MODULE_LIBRARY")
                rx_add_link_item("$<TARGET_LINKER_FILE:${lib}>")
            endif()

            get_target_property(interface_libs "${lib}" INTERFACE_LINK_LIBRARIES)
            if(interface_libs)
                rx_walk_link_libs("${interface_libs}")
            endif()
        else()
            rx_add_link_item("${lib}")
        endif()
    endforeach()
endfunction()

get_target_property(RX_DIRECT_LIBS raptor_cpp LINK_LIBRARIES)
if(RX_DIRECT_LIBS)
    rx_walk_link_libs("${RX_DIRECT_LIBS}")
endif()

get_property(RX_LINK_ITEMS GLOBAL PROPERTY RX_LINK_ITEMS)
get_target_property(RX_LINK_DIRS raptor_cpp LINK_DIRECTORIES)
get_target_property(RX_BUILD_RPATH raptor_cpp BUILD_RPATH)

set(RX_LINK_SPEC "ARCHIVE $<TARGET_FILE:raptor_cpp>\n")
foreach(dir IN LISTS RX_LINK_DIRS)
    get_filename_component(dir "${dir}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    string(APPEND RX_LINK_SPEC "DIR ${dir}\n")
    string(APPEND RX_LINK_SPEC "RPATH ${dir}\n")
endforeach()
foreach(dir IN LISTS RX_BUILD_RPATH)
    string(APPEND RX_LINK_SPEC "RPATH ${dir}\n")
endforeach()
foreach(item IN LISTS RX_LINK_ITEMS)
    string(APPEND RX_LINK_SPEC "LIB ${item}\n")
endforeach()

file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/raptor_cpp.$<CONFIG>.link" CONTENT "${RX_LINK_SPEC}")
