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


// static constexpr const char* scGlyphMap = "ABCDEFGHIJKLM"
// 										  "NOPQRSTUVWXYZ"
// 										  "abcdefghijklm"
// 										  "nopqrstuvwxyz"
// 										  "0123456789+-=";
//
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
	if (mpAtlas != nullptr) {
		return;
	}

	mpAtlas = gTextureManager->NewTexture();

	mAtlasTicket = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm, "Textures/debug_font.png",
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

void TextRenderer::DrawText(const char* text, float32 scale, uint32 color)
{
	if (mpAtlas == nullptr || text == nullptr) {
		return;
	}

	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	const renderer::Pipeline& pipeline = gPipelineCache->Request(ePipelineName::TextRendering);

	const uint32 frame_number = gGraphics->GetFrameNumber();
	if (frame_number != mLastFrameNumber) {
		mLastFrameNumber = frame_number;
		mTapeOffset = 0;
		mCursorPosition = Vec2f::sZero;
	}

	const uint32 base_offset = (frame_number * scMaxGlyphs * sizeof(InstanceData));

	gPipelineCache->AddBufferOffset(0, base_offset);
	gPipelineCache->Bind(ePipelineName::TextRendering, cmd);

	mpDS->Bind(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	TextPushConstants consts {};
	memcpy(consts.CombinedMatrix, mOrthoProjection.RawData, sizeof(consts.CombinedMatrix));
	consts.TextColor = color;
	consts.InstanceBase = mTapeOffset / sizeof(InstanceData);
	gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, consts);

	const float32 glyph_size = static_cast<float32>(scGlyphWidth) * scale;

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
			cursor.X += glyph_size;
			continue;
		}

		const uint32 col = static_cast<uint32>(glyph_index % scAtlasColumns);
		const uint32 row = static_cast<uint32>(glyph_index / scAtlasColumns);

		InstanceData* inst = instances.Insert();
		inst->vPosition[0] = cursor.X;
		inst->vPosition[1] = -cursor.Y;
		inst->vSize[0] = glyph_size;
		inst->vSize[1] = glyph_size;
		inst->UVMin[0] = (static_cast<float32>(col) * scGlyphWidth) / atlas_w;
		inst->UVMin[1] = (static_cast<float32>(row) * scGlyphHeight) / atlas_h;
		inst->UVMax[0] = (static_cast<float32>(col + 1) * scGlyphWidth) / atlas_w;
		inst->UVMax[1] = (static_cast<float32>(row + 1) * scGlyphHeight) / atlas_h;

		cursor.X += glyph_size;
	}

	if (instances.Size == 0) {
		return;
	}

	if (mTapeOffset + instances.Size * sizeof(InstanceData) > scMaxGlyphs * sizeof(InstanceData)) {
		LogWarning(LC_RENDER, "TextRenderer: Too many glyphs in frame, dropping text");
		return;
	}

	uint8* mapped = reinterpret_cast<uint8*>(mInstanceBuffer.pMappedBuffer);
	memcpy(mapped + base_offset + mTapeOffset, instances.pData, instances.Size * sizeof(InstanceData));

	mTapeOffset += instances.Size * sizeof(InstanceData);

	mpQuad->Render(cmd, instances.Size);

	mCursorPosition.Y += scGlyphHeight * scale;
}

} // namespace fx::renderer
