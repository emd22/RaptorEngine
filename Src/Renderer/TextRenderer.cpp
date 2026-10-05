#include "TextRenderer.hpp"

#include "Backend/Commands.hpp"
#include "Backend/DescriptorCache.hpp"
#include "Backend/Image.hpp"
#include "Backend/Sampler/Sampler.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "Constants.hpp"
#include "Globals.hpp"
#include "GraphicsBackend.hpp"
#include "PipelineCache.hpp"
#include "PrimitiveMesh.hpp"


#include <Asset/AssetManager.hpp>
#include <Asset/MeshGen.hpp>
#include <Core/StackArray.hpp>
#include <Math/Mat4.hpp>
#include <Texture/TextureManager.hpp>

namespace fx::renderer {
FX_SET_MODULE_NAME("TextRenderer")


static_assert(sizeof(TextRenderer::InstanceData) == 8 * sizeof(float32));

TextRenderer::TextRenderer()
{
	AssertEqual(rx_text_glyph_width(), scGlyphWidth);
	AssertEqual(rx_text_glyph_height(), scGlyphHeight);
	AssertEqual(rx_text_max_glyphs(), scMaxGlyphs);

	mpState = rx_text_state_new();
}

TextRenderer::~TextRenderer()
{
	Destroy();

	rx_text_state_free(mpState);
	mpState = nullptr;
}

void TextRenderer::Create()
{
	if (mpAtlas != nullptr || mAtlasTicket.IsValid()) {
		return;
	}

	mAtlasTicket = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm, "Textures/debug_font3.png",
											eImageCreateFlags::None);

	mAtlasTicket.OnLoaded(
		[&](void* data)
		{
			Image* texture = reinterpret_cast<Image*>(data);
			mpAtlas = texture;

			SizedArray<DescriptorEntry> entries = {
				DescriptorEntry::AsBuffer(0, eShaderType::Vertex, &GetInstanceBuffer(), 0,
										  sizeof(TextRenderer::InstanceData) * TextRenderer::scMaxGlyphs),
				DescriptorEntry::AsImage(1, eShaderType::Pixel, texture,
										 gSamplerCache->Request({ eSamplerFilter::Nearest, eSamplerFilter::Nearest,
																  eSamplerFilter::Nearest }))
			};
			mpDS = gDescriptorCache->Request(entries).second;
		});


	Ref<MeshGen::GeneratedMesh> gm = MeshGen::MakeQuad(Vec2f { 1.0f, 1.0f });
	mpQuad = gm->AsDefaultMesh();

	Resize();

	mInstanceBuffer.Create(eGpuBufferType::StorageWithOffset,
						   static_cast<uint64>(scMaxGlyphs) * sizeof(InstanceData) * renderer::FramesInFlight,
						   RX_MEMORY_CPU_ONLY, eGpuBufferFlags::PersistentMapped);
}


void TextRenderer::Resize()
{
	const Vec2u size = gGraphics->GetWindow()->GetSize();

	float values[16];
	rx_text_ortho(static_cast<float32>(size.X), static_cast<float32>(size.Y), 0.1f, 10.0f, values);

	mOrthoProjection = Mat4f::FromRows(values);
}

void TextRenderer::Destroy()
{
	if (mInstanceBuffer.Initialized) {
		mInstanceBuffer.Destroy();
	}

	mpAtlas = nullptr;
	mpQuad = nullptr;
}

bool TextRenderer::SubmitQuads(DescriptorSet* ds, const InstanceData* instances, uint32 count, uint32 color,
							   bool is_image)
{
	const uint32 instances_size = count * sizeof(InstanceData);

	const uint32 tape_offset = rx_text_reserve(mpState, count);

	if (tape_offset == RX_TEXT_NO_ROOM) {
		LogWarning(LC_RENDER, "TextRenderer: Too many quads in frame, dropping draw");
		return false;
	}

	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	const ePipelineName pipeline_name = is_image ? ePipelineName::ImageRendering : ePipelineName::TextRendering;
	const renderer::Pipeline& pipeline = gPipelineCache->Request(pipeline_name);

	// Each frame in flight has its own region of the instance buffer
	const uint32 base_offset = (gGraphics->GetFrameNumber() * scMaxGlyphs * sizeof(InstanceData));

	uint8* mapped = reinterpret_cast<uint8*>(mInstanceBuffer.GetMapped());
	memcpy(mapped + base_offset + tape_offset, instances, instances_size);

	gPipelineCache->AddBufferOffset(0, base_offset);
	gPipelineCache->Bind(pipeline_name, cmd);

	ds->Bind(0, cmd, pipeline, Slice<const uint32>(&base_offset, 1));

	TextPushConstants consts {};
	memcpy(consts.CombinedMatrix, mOrthoProjection.RawData, sizeof(consts.CombinedMatrix));
	consts.TextColor = color;
	consts.InstanceBase = tape_offset / sizeof(InstanceData);
	gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, consts);

	mpQuad->Render(cmd, count);

	return true;
}

void TextRenderer::DrawText(const char* text, float32 scale, uint32 color)
{
	if (mpAtlas == nullptr || text == nullptr) {
		return;
	}

	rx_text_begin_frame_if_needed(mpState, gGraphics->GetFrameNumber());

	Vec2f cursor;
	rx_text_cursor(mpState, &cursor.X, &cursor.Y);

	DrawTextFrom(text, cursor, scale, color, true);
}

void TextRenderer::DrawTextFrom(const char* text, Vec2f origin, float32 scale, uint32 color, bool advance_cursor)
{
	const Vec2u window_size = gGraphics->GetWindow()->GetSize();
	const Vec2u atlas_size = mpAtlas->GetSize();

	InstanceData instances[scMaxGlyphs];
	float32 line_height = 0.0f;

	const size_t count = rx_text_layout(text, scale, origin.X, origin.Y, window_size.X, window_size.Y, atlas_size.X,
										atlas_size.Y, instances, scMaxGlyphs, &line_height);

	if (count == 0) {
		return;
	}

	if (!SubmitQuads(mpDS, instances, static_cast<uint32>(count), color, false)) {
		return;
	}

	if (advance_cursor) {
		rx_text_move_cursor_down(mpState, line_height);
	}
}

void TextRenderer::DrawTextAt(const char* text, Vec2f position, float32 scale, uint32 color)
{
	if (mpAtlas == nullptr || text == nullptr) {
		return;
	}

	rx_text_begin_frame_if_needed(mpState, gGraphics->GetFrameNumber());

	// The text is placed from `position` without touching where the next line of the frame's text goes
	DrawTextFrom(text, position - scMargin, scale, color, false);
}

void TextRenderer::DrawImage(Image* image, Vec2f position, Vec2f size, uint32 color)
{
	if (image == nullptr) {
		return;
	}

	rx_text_begin_frame_if_needed(mpState, gGraphics->GetFrameNumber());

	// The shader filters images itself from point taps, clamped to a transparent border so taps past the edge of the
	// image fade out instead of wrapping
	SamplerProps sampler_props {};
	sampler_props.SetNearest();
	sampler_props.AddressMode = eSamplerAddressMode::ClampToBorder;
	sampler_props.BorderColor = eSamplerBorderColor::FloatTransparent;

	// Cached by image, so this only builds the set on the first draw
	SizedArray<DescriptorEntry> entries = {
		DescriptorEntry::AsBuffer(0, eShaderType::Vertex, &mInstanceBuffer, 0, sizeof(InstanceData) * scMaxGlyphs),
		DescriptorEntry::AsImage(1, eShaderType::Pixel, image, gSamplerCache->Request(sampler_props)),
	};
	DescriptorSet* ds = gDescriptorCache->Request(entries).second;

	// Instances are placed by their bottom-left corner, relative to the centre of the window with +Y up
	const Vec2u window_size = gGraphics->GetWindow()->GetSize();

	InstanceData instance;
	rx_text_image_instance(position.X, position.Y, size.X, size.Y, window_size.X, window_size.Y, &instance);

	SubmitQuads(ds, &instance, 1, color, true);
}

} // namespace fx::renderer
