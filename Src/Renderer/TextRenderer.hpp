#pragma once

#include <Asset/AssetTicket.hpp>
#include <Core/Ref.hpp>
#include <Math/Mat4.hpp>
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
	/// Max glyphs on screen at once
	static constexpr uint32 scMaxGlyphs = 256;
	static constexpr uint32 scGlyphWidth = 6;
	static constexpr uint32 scGlyphHeight = 12;
	static constexpr uint32 scAtlasColumns = 16;
	static constexpr uint32 scAtlasRows = 6;

	static constexpr Vec2f scMargin = Vec2f(20.0f);

	struct InstanceData
	{
		float vPosition[2];
		float Size[2];
		float UVMin[2];
		float UVMax[2];
	};

public:
	TextRenderer() = default;
	TextRenderer(TextRenderer& other) = delete;
	TextRenderer operator=(TextRenderer& other) = delete;

	void Create();
	void Resize();
	void Destroy();

	void DrawText(const char* text, float32 scale, uint32 color);

	Image* GetAtlas() const { return mpAtlas; }
	RawGpuBuffer& GetInstanceBuffer() { return mInstanceBuffer; }

	~TextRenderer();

private:
	Ref<PrimitiveMesh> mpQuad;
	Image* mpAtlas = nullptr;

	DescriptorSet* mpDS = nullptr;
	RawGpuBuffer mInstanceBuffer;

	AssetTicket mAtlasTicket { nullptr };

	uint32 mLastFrameNumber = 0;

	/// The offset from the start of the current buffer. This increases for each call to `Render` in a single frame.
	/// Think of it as an ink ribbon, where the characters are used and the cursor moves forward.
	uint32 mTapeOffset = 0;

	Vec2f mCursorPosition = Vec2f::sZero;

	Mat4f mOrthoProjection = Mat4f::scIdentity;
};

} // namespace fx::renderer
