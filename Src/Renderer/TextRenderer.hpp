#pragma once

#include <Asset/AssetTicket.hpp>
#include <Core/Ref.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>
#include <Renderer/PrimitiveMesh.hpp>


namespace fx {

class Image;

} // namespace fx

namespace fx::renderer {

class CommandBuffer;
class Sampler;


class TextRenderer
{
public:
	static constexpr uint32 scMaxGlyphs = 128;
	static constexpr uint32 scGlyphTexels = 6;
	static constexpr uint32 scAtlasColumns = 13;
	static constexpr uint32 scAtlasRows = 7;

	struct InstanceData
	{
		float vPosition[2];
		float vSize[2];
		float vUvMin[2];
		float vUvMax[2];
	};

public:
	TextRenderer() = default;
	TextRenderer(TextRenderer& other) = delete;
	TextRenderer operator=(TextRenderer& other) = delete;

	void Create();
	void Destroy();

	void Render(const CommandBuffer& cmd, const char* text, const Vec2f& position, float32 scale, uint32 uiColor);

	Image* GetAtlas() const { return mpAtlas; }
	RawGpuBuffer& GetInstanceBuffer() { return mInstanceBuffer; }

	~TextRenderer();

private:
	Ref<PrimitiveMesh> mpQuad;
	Image* mpAtlas = nullptr;

	DescriptorSet* mpDS = nullptr;
	RawGpuBuffer mInstanceBuffer;

	AssetTicket mAtlasTicket { nullptr };
};

} // namespace fx::renderer
