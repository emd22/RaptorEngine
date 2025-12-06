# Foxtrot Engine
Foxtrot is a 3D game engine being developed for an experimental game.


## Features
- Fast math library using SIMD
	- **NOTE**: This currently only supports ARM Neon, but SSE will be supported soon.
- Vulkan rendering backend
- Super fast custom memory pool
- Multithreaded and extensible asset manager that works seamlessly in the background
- Custom core library and containers
- GLTF loading with cgltf, JPEG loading with turbojpeg and other format loading using stb-image
- Integration with Jolt Physics
- Deferred rendering with light volumes

## Platforms
Currently Foxtrot **only supports macOS and arm64**. There will be x86_64 and Windows support in the future, but I am currently focusing my time on other parts of the engine.

## Screenshots

**PBR Lighting**
<!--<img src="Screenshots/00_PbrLitHelmet.png" width="300">-->
![Normalmapped cube](Screenshots/01_NormalmappedCube.png)
![Damaged helmet model with red and white lights lighting it](Screenshots/00_PbrLitHelmet.png)
