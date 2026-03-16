workspace "foxtrot"
    configurations { "Debug", "Release" }
    targetdir "build/%{cfg.buildcfg}"
    cppdialect "C++20"
	
	if _ACTION and _ACTION:startswith("vs") then 
		platforms {"x64"}
	else
		platforms {"x64", "macOS"}
	end

    filter {"platforms:x64"}
        system "windows"
        architecture "x86_64"
        buildoptions { "/MP", "/arch:AVX2", "/Oi" }
        libdirs { "./Lib/Win32" }

    filter {"platforms:macOS"}
        system "macosx"
        architecture "aarch64"
        buildoptions {"-Wno-nullability-completeness"}
        libdirs { "./Lib/macOS" }
	
	
    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"
	
	filter {}
	
    newoption {
        trigger = "simde",
        description = "Use SIMDe to use AVX on a non-AVX platform"
    }
	
	
project "foxtrot"
    kind "ConsoleApp"
    language "C++"
    targetdir "build/%{cfg.buildcfg}"
	objdir "buildobj/%{cfg.buildcfg}"
	libdirs {"$(VULKAN_SDK)/Lib"}
  
    files {"Src/**.cpp", "Src/**.inl", "Src/**.hpp", "Src/ThirdParty/**.cpp", "Src/ThirdParty/**.h", "Src/ThirdParty/**.hpp", "Src/ThirdParty/**.inl"}

    includedirs { "Src", "Src/ThirdParty", "Lib/Include", "Lib/Include", "$(VULKAN_SDK)/include" }
    links {"vulkan-1", "slang", "turbojpeg", "SDL3"}

    defines { "FX_BASE_DIR=\"" .. _MAIN_SCRIPT_DIR .. "\"", "_USE_MATH_DEFINES" }

    -- Enable SIMDE to test the AVX math functions
    filter "options:simde"
        defines { "FX_USE_SIMDE", "SIMDE_ENABLE_NATIVE_ALIASES" }

    filter "platforms:macOS"
        defines { "VK_ENABLE_BETA_EXTENSIONS=1" }

	filter {}
