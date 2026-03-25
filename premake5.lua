workspace "foxtrot"
    configurations { "DebugRelease", "Debug", "Release" }
    targetdir "build/%{cfg.buildcfg}"
    cppdialect "C++20"



	if _ACTION and _ACTION:startswith("vs") then
		platforms {"x64"}
	else
		platforms {"macOS"}
	end

	multiprocessorcompile "On"

    filter {"platforms:x64"}
        system "windows"
        architecture "x86_64"
        buildoptions {  "/Oi" }
    	vectorextensions "AVX2"
        libdirs { "./Lib/Win32" }

    filter {"platforms:macOS"}
        system "macosx"
        architecture "aarch64"
        buildoptions {"-Wno-nullability-completeness"}
        libdirs { "./Lib/macOS" }


    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"

    filter "configurations:DebugRelease"
        defines { "NDEBUG" }
        symbols "On"
        -- linktimeoptimization "On"
        optimize "On"

    filter "configurations:Release"
        defines { "NDEBUG" }
        linktimeoptimization "On"
        optimize "Speed"
        runtimechecks       "Off"
		buffersecuritycheck "Off"

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


    files {
        "Src/**.hpp", "Src/**.inl", "Src/**.cpp",

        "Src/ThirdParty/**.cpp", "Src/ThirdParty/**.h",
        "Src/ThirdParty/**.hpp", "Src/ThirdParty/**.inl"
    }

	if os.host() == "windows" then
	    libdirs {"$(VULKAN_SDK)/Lib"}
        links {"vulkan-1", "dxcompiler"}
		includedirs {"$(VULKAN_SDK)/include"}
	else
	    local vk_env = os.getenv("VULKAN_SDK");
	    libdirs {vk_env .. "/lib"}
		runpathdirs {vk_env .. "/lib"}
		links {"vulkan.1", "dxcompiler"}
		includedirs {vk_env .. "/include"}
		externalincludedirs {vk_env .. "/include"}
	end


    includedirs { "Src", "Src/ThirdParty", "Lib/Include", "Lib/Include" }
    externalincludedirs { "Src", "Src/ThirdParty", "Lib/Include", "Lib/Include" }
    links {"turbojpeg", "SDL3"}

    defines { "FX_BASE_DIR=\"" .. _MAIN_SCRIPT_DIR .. "\"", "_USE_MATH_DEFINES" }

    -- Enable SIMDE to test the AVX math functions
    filter "options:simde"
        defines { "FX_USE_SIMDE", "SIMDE_ENABLE_NATIVE_ALIASES" }

    filter "platforms:macOS"
        defines { "VK_ENABLE_BETA_EXTENSIONS=1" }
	filter {}

    cleancommands {"ninja -t clean"}
