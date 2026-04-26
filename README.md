# Foxtrot Engine
Foxtrot is a 3D game engine being developed for an experimental game.


## Features
- Fast math library using SIMD
	- Supports Arm NEON and SSE/AVX.
- Vulkan graphics backend
- Custom scripting language
- Super fast memory pool
- Custom core library and containers
- Multithreaded and extensible asset manager that works seamlessly in the background
- Jolt Physics
- Deferred rendering with light volumes

## Building
To build the engine, make sure you have CMake installed.

### Building for MacOS
You can use `cmake` to generate the project files. To build with Ninja, you use one of the generated build targets.
For example,
```
# Generate the project or build files
cmake -GNinja -DUSE_SIMDE=Off -DUSE_MOLTENVK=On .

# Build Foxtrot
ninja

# Run the executable. Replace `Debug` with the optimization level you built with.
./build/Debug/foxtrot
```


## Platforms Supported
- Windows (x86_64)
- macOS (aarch64)

## Screenshots
**PBR Lighting**
![Updated lighting](Screenshots/05_SkyboxShadows.png)

![Normalmapped cube](Screenshots/01_NormalMappedCube.png)
<!--<img src="Screenshots/00_PbrLitHelmet.png" width="300">-->
![Damaged helmet model with red and white lights lighting it](Screenshots/00_PbrLitHelmet.png)
