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

	/// The size of a glyph in the font atlas, which the layout in Rust uses too, see the check in the constructor
	static constexpr uint32 scGlyphWidth = 6;
	static constexpr uint32 scGlyphHeight = 12;

	static constexpr Vec2f scMargin = Vec2f(20.0f);

	using InstanceData = RxTextInstance;

public:
	TextRenderer();
	TextRenderer(const TextRenderer& other) = delete;
	TextRenderer& operator=(const TextRenderer& other) = delete;

	void Create();
	void Resize();
	void Destroy();

	void DrawText(const char* text, float32 scale, uint32 color);

	/**
	 * @brief Draws `text` with its top-left corner at `position` in window pixels
	 */
	void DrawTextAt(const char* text, Vec2f position, float32 scale, uint32 color);

	/**
	 * @brief Draws `image` as a screen-space quad tinted by `color`, for HUD elements like the crosshair.
	 * @param position The top-left corner in window pixels, measured down from the top-left of the window.
	 * @param size The size of the quad in window pixels.
	 */
	void DrawImage(Image* image, Vec2f position, Vec2f size, uint32 color);

	Image* GetAtlas() const { return mpAtlas; }
	RawGpuBuffer& GetInstanceBuffer() { return mInstanceBuffer; }

	~TextRenderer();

private:
	/// Lays `text` out from `origin` and draws it, moving the frame's text cursor down a line if `advance_cursor`.
	void DrawTextFrom(const char* text, Vec2f origin, float32 scale, uint32 color, bool advance_cursor);

	/// Copies `instances` onto the tape and draws them, returns false if the frame's tape is full.
	bool SubmitQuads(DescriptorSet* ds, const InstanceData* instances, uint32 count, uint32 color, bool is_image);

	Ref<PrimitiveMesh> mpQuad;
	Image* mpAtlas = nullptr;

	DescriptorSet* mpDS = nullptr;
	RawGpuBuffer mInstanceBuffer;

	AssetTicket mAtlasTicket { nullptr };

	/// How much of the frame's instance buffer is used, and where the next line of text goes. Think of the buffer as an
	/// ink ribbon, where the characters are used and the cursor moves forward. This is in Rust, along with the layout
	/// of the text into quads.
	RxTextState* mpState = nullptr;

	Mat4f mOrthoProjection = Mat4f::scIdentity;
};

} // namespace fx::renderer
