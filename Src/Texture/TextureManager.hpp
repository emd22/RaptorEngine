#pragma once

#include "TextureID.hpp"

#include <Core/Bitset.hpp>
#include <Core/FreeArray.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>
#include <Renderer/Backend/Image.hpp>

namespace fx {

class TextureManager
{
public:
	static constexpr uint32 scMaxTextures = 512;


public:
	void Create();

	TextureID NewTextureID();
	renderer::Image* NewTexture();
	renderer::Image* GetTexture(TextureID id);

	void DestroyTexture(TextureID id);
	void ReleaseAllTextures();

	void Destroy();

	~TextureManager() { Destroy(); }

public:
	std::mutex mInUse;

private:
	FreeArray<renderer::Image> mTextureList;
};

} // namespace fx
