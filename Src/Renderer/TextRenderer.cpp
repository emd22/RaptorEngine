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

#include <ThirdParty/stb_image.h>

#include <Asset/AssetManager.hpp>
#include <Asset/MeshGen.hpp>
#include <Core/StackArray.hpp>
#include <Math/Mat4.hpp>
#include <Texture/TextureManager.hpp>

namespace fx::renderer {
FX_SET_MODULE_NAME("TextRenderer")


static constexpr const char* scGlyphMap = " !\"#$%&'()*+,-./"
										  "0123456789:;<=>?"
										  "@ABCDEFGHIJKLMNO"
										  "PQRSTUVWXYZ[\\]^_"
										  "`abcdefghijklmno"
										  "pqrstuvwxyz{|}~ ";

static int FindGlyphIndex(char c)
{
	for (int i = 0; scGlyphMap[i] != '\0'; ++i) {
		if (scGlyphMap[i] == c) {
			return i;
		}
	}

	return -1;
}

TextRenderer::~TextRenderer() { Destroy(); }

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

	Vec2u window_size = gGraphics->GetWindow()->GetSize();
	mOrthoProjection.LoadOrthographicMatrix(window_size.X, window_size.Y, 0.1f, 10.0f);

	mInstanceBuffer.Create(eGpuBufferType::StorageWithOffset,
						   static_cast<uint64>(scMaxGlyphs) * sizeof(InstanceData) * renderer::FramesInFlight,
						   VMA_MEMORY_USAGE_CPU_ONLY, eGpuBufferFlags::PersistentMapped);
}


void TextRenderer::Resize()
{
	Vec2u size = gGraphics->GetWindow()->GetSize();
	mOrthoProjection.LoadOrthographicMatrix(size.X, size.Y, 0.1f, 10.0f);
}

void TextRenderer::Destroy()
{
	if (mInstanceBuffer.Initialized) {
		mInstanceBuffer.Destroy();
	}

	mpAtlas = nullptr;
	mpQuad = nullptr;
}

void TextRenderer::BeginFrameIfNeeded()
{
	const uint32 frame_number = gGraphics->GetFrameNumber();

	if (frame_number != mLastFrameNumber) {
		mLastFrameNumber = frame_number;
		mTapeOffset = 0;
		mCursorPosition = Vec2f::sZero;
	}
}

bool TextRenderer::SubmitQuads(DescriptorSet* ds, const InstanceData* instances, uint32 count, uint32 color,
							   bool is_image)
{
	const uint32 instances_size = count * sizeof(InstanceData);

	if (mTapeOffset + instances_size > scMaxGlyphs * sizeof(InstanceData)) {
		LogWarning(LC_RENDER, "TextRenderer: Too many quads in frame, dropping draw");
		return false;
	}

	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	const renderer::Pipeline& pipeline = gPipelineCache->Request(ePipelineName::TextRendering);

	// Each frame in flight has its own region of the instance buffer
	const uint32 base_offset = (gGraphics->GetFrameNumber() * scMaxGlyphs * sizeof(InstanceData));

	uint8* mapped = reinterpret_cast<uint8*>(mInstanceBuffer.pMappedBuffer);
	memcpy(mapped + base_offset + mTapeOffset, instances, instances_size);

	gPipelineCache->AddBufferOffset(0, base_offset);
	gPipelineCache->Bind(ePipelineName::TextRendering, cmd);

	ds->Bind(0, cmd, pipeline, Slice<const uint32>(&base_offset, 1));

	TextPushConstants consts {};
	memcpy(consts.CombinedMatrix, mOrthoProjection.RawData, sizeof(consts.CombinedMatrix));
	consts.TextColor = color;
	consts.InstanceBase = mTapeOffset / sizeof(InstanceData);
	consts.IsImage = is_image ? 1 : 0;
	gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, consts);

	mTapeOffset += instances_size;

	mpQuad->Render(cmd, count);

	return true;
}

void TextRenderer::DrawText(const char* text, float32 scale, uint32 color)
{
	if (mpAtlas == nullptr || text == nullptr) {
		return;
	}

	BeginFrameIfNeeded();

	const float32 glyph_width = static_cast<float32>(scGlyphWidth) * scale;
	const float32 glyph_height = static_cast<float32>(scGlyphHeight) * scale;

	StackArray<InstanceData, scMaxGlyphs> instances;
	const float32 atlas_w = static_cast<float32>(mpAtlas->Info.Size.X);
	const float32 atlas_h = static_cast<float32>(mpAtlas->Info.Size.Y);


	Vec2u half_window_size = (gGraphics->GetWindow()->GetSize() / 2U);

	Vec2f cursor = mCursorPosition - Vec2f(float32(half_window_size.X), float32(half_window_size.Y)) + scMargin;

	for (const char* c = text; *c != '\0'; ++c) {
		if (instances.Size >= scMaxGlyphs) {
			break;
		}

		const int glyph_index = FindGlyphIndex(*c);

		if (glyph_index < 0) {
			cursor.X += glyph_width;
			continue;
		}

		const uint32 col = static_cast<uint32>(glyph_index % scAtlasColumns);
		const uint32 row = static_cast<uint32>(glyph_index / scAtlasColumns);

		InstanceData* inst = instances.Insert();
		inst->vPosition[0] = cursor.X;
		inst->vPosition[1] = -cursor.Y;
		inst->Size[0] = glyph_width;
		inst->Size[1] = glyph_height;
		inst->UVMin[0] = (static_cast<float32>(col) * scGlyphWidth) / atlas_w;
		inst->UVMin[1] = (static_cast<float32>(row) * scGlyphHeight) / atlas_h;
		inst->UVMax[0] = (static_cast<float32>(col + 1) * scGlyphWidth) / atlas_w;
		inst->UVMax[1] = (static_cast<float32>(row + 1) * scGlyphHeight) / atlas_h;

		cursor.X += glyph_width;
	}

	if (instances.Size == 0) {
		return;
	}

	if (!SubmitQuads(mpDS, instances.pData, instances.Size, color, false)) {
		return;
	}

	mCursorPosition.Y += glyph_height;
}

void TextRenderer::DrawImage(Image* image, Vec2f position, Vec2f size, uint32 color)
{
	if (image == nullptr) {
		return;
	}

	BeginFrameIfNeeded();

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
	const float32 half_width = static_cast<float32>(window_size.X) * 0.5f;
	const float32 half_height = static_cast<float32>(window_size.Y) * 0.5f;

	InstanceData instance {
		.vPosition = { position.X - half_width, half_height - (position.Y + size.Y) },
		.Size = { size.X, size.Y },
		.UVMin = { 0.0f, 0.0f },
		.UVMax = { 1.0f, 1.0f },
	};

	SubmitQuads(ds, &instance, 1, color, true);
}

} // namespace fx::renderer
