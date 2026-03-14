workspace "foxtrot"
    configurations { "Debug", "RelWithDebInfo", "Release" }
    targetdir "build/%{cfg.buildcfg}"
    platforms {"Win64", "macOS"}
    cppdialect ("C++20")


    filter {"platforms:Win64"}
        system "windows"
        architecture "x86_64"
        buildoptions { "/MP", "/arch:AVX2", "/Oi" }
        libdirs { "./Lib/Win32" }


    filter {"platforms:macOS"}
        system "macosx"
        architecture "arm64"
        buildoptions {"-Wno-nullability-completeness"}
        libdirs { "./Lib/macOS" }

    newoption {
        trigger = "simde",
        description = "Use SIMDe to use AVX on a non-AVX platform"
    }

-- Third party files, compiled into static library
project "tplib"
    kind "StaticLib"
    language "C++"

    files {"Src/ThirdParty/**.cpp", "Src/ThirdParty/**.h", "Src/ThirdParty/**.hpp", "Src/ThirdParty/**.inl"}
    includedirs {"Lib/Include", "Src/ThirdParty"}

    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"

    filter "configurations:RelWithDebInfo"
        defines { "NDEBUG" }
        symbols "On"
        optimize "On"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"


project "foxtrot"
    kind "ConsoleApp"
    language "C++"
    targetdir "build/%{cfg.buildcfg}"

    files {"Src/**.cpp", "Src/**.inl", "Src/**.hpp"}

    includedirs { "Src", "Src/ThirdParty", "Lib/Include" }
    libdirs { "build/%{cfg.buildcfg}" }
    links {"tplib", "Vulkan", "slang", "turbojpeg", "SDL3"}

    defines { "FX_BASE_DIR=\"" .. _MAIN_SCRIPT_DIR .. "\"", "_USE_MATH_DEFINES" }

    -- Enable SIMDE to test the AVX math functions
    filter "options:simde"
        defines { "FX_USE_SIMDE", "SIMDE_ENABLE_NATIVE_ALIASES" }

    filter "platforms:macOS"
        defines { "VK_ENABLE_BETA_EXTENSIONS=1" }

    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"

    filter "configurations:RelWithDebInfo"
        defines { "NDEBUG" }
        symbols "On"
        optimize "On"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"
