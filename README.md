![](Screenshots/raptor-logo2.png)

Raptor is a 3D game engine being developed for an experimental game.

## Screenshots

|          Global Illumination (Bright)          |       Global Illumination (Dark)        |
| :--------------------------------------------: | :-------------------------------------: |
| ![GI test scene](Screenshots/11_GI_Bright.png) | ![After GI](Screenshots/12_GI_Dark.png) |

## Features

- Tiled forward renderer (Forward+) with Vulkan
- Blockout editor for building prototype levels fast
- Baked light probe global illumination
- Fast math library using SIMD
    - Supports Arm NEON and AVX processors.

- Scripting with [Strata](https://github.com/StrataLanguage/stratac), compiled JIT
    - All in-game editor modes are written in Strata.
- Custom core library and containers
- Multithreaded and extensible asset manager that works seamlessly in the background
- Jolt Physics integration

## Docs

| Name          | Document                                |
| ------------- | --------------------------------------- |
| Config format | [ConfigFormat.md](Docs/ConfigFormat.md) |

## Building

TODO

## Platforms Supported

- Windows (x86_64)
- macOS (aarch64)
