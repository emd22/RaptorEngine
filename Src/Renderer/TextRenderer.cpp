#include "TextRenderer.hpp"

#include "Backend/Commands.hpp"
#include "Backend/Image.hpp"
#include "Backend/Sampler/Sampler.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "Constants.hpp"
#include "Globals.hpp"
#include "GraphicsBackend.hpp"
#include "PipelineCache.hpp"
#include "PrimitiveMesh.hpp"

#include <ThirdParty/stb_image.h>

#include <Asset/MeshGen.hpp>
#include <Core/StackArray.hpp>
#include <Math/Mat4.hpp>
#include <Texture/TextureManager.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("TextRenderer")

/// Row-major glyph map for the confident portion of the atlas (rows 0-4).
/// Each glyph slot is 6x6 texels; the atlas is 13 columns wide.
static constexpr const char* scGlyphMap = "ABCDEFGHIJKLM"
										  "NOPQRSTUVWXYZ"
										  "abcdefghijklm"
										  "nopqrstuvwxyz"
										  "0123456789+-=";

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

	// Load and decode the bitmap font atlas.
	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_uc* pixels = stbi_load("Textures/square_6x6.png", &width, &height, &channels, STBI_rgb_alpha);
	if (pixels == nullptr) {
		LogError("Could not load 'Textures/square_6x6.png'.");
		return;
	}

	mpAtlas = gTextureManager->NewTexture();

	const uint64 data_size = static_cast<uint64>(width) * height * 4;

	gGraphics->SubmitImmediateUploadCmd(
		[&](renderer::CommandBuffer& cmd)
		{
			ImageInfo info(Vec2u { static_cast<uint32>(width), static_cast<uint32>(height) }, eImageFormat::RGBA8_UNorm,
						   0, 1, Slice<const uint8>(pixels, data_size));
			mpAtlas->CreateFromData(cmd, info, eImageCreateFlags::None);
		});

	stbi_image_free(pixels);

	// Nearest sampling so the pixel font stays crisp.
	mpSampler = gSamplerCache->Request(SamplerProps {}.SetNearest());

	// Unit quad used as the per-glyph geometry (degenerate UVs are overridden per-instance).

	Ref<MeshGen::GeneratedMesh> gm = MeshGen::MakeQuad({});
	mpQuad = gm->AsDefaultMesh();

	// CPU-writable persistent-mapped instance storage with one page per in-flight frame
	// (mirrors the engine's ObjectManager::mObjectGpuBuffer pattern).
	mInstanceBuffer.Create(eGpuBufferType::StorageWithOffset,
						   static_cast<uint64>(scMaxGlyphs) * sizeof(InstanceData) * renderer::FramesInFlight,
						   VMA_MEMORY_USAGE_CPU_ONLY, eGpuBufferFlags::PersistentMapped);
}

void TextRenderer::Destroy()
{
	if (mInstanceBuffer.Initialized) {
		mInstanceBuffer.Destroy();
	}

	mpAtlas = nullptr;
	mpSampler = nullptr;
	mpQuad = nullptr;
}

void TextRenderer::Render(const CommandBuffer& cmd, const char* text, const Vec2f& position, float32 scale,
						  uint32 uiColor)
{
	if (mpAtlas == nullptr || text == nullptr) {
		return;
	}

	const renderer::Pipeline& pipeline = gPipelineCache->Request(ePipelineName::TextRendering);
	// pipeline.Bind(cmd);

	// Reset instance counter on new frame.
	const uint32 current_frame = gGraphics->GetFrameNumber();
	if (current_frame != mLastFrame) {
		mInstancesWritten = 0;
		mLastFrame = current_frame;
	}

	const uint32 base_offset = current_frame * scMaxGlyphs * sizeof(InstanceData);
	const uint32 write_offset = base_offset + mInstancesWritten * sizeof(InstanceData);

	gPipelineCache->AddBufferOffset(0, write_offset);
	gPipelineCache->Bind(ePipelineName::TextRendering, cmd);

	// Bind descriptor sets (instance buffer + font atlas).
	// for (const Pipeline::DescriptorRef& desc_ref : pipeline.DescriptorIDs) {
	// 	if (desc_ref.SetIndex == 0) {
	// 		const uint32 offsets[] = { 0 };
	// 		desc_ref.pSet->Bind(0, cmd, pipeline, Slice<const uint32>(offsets, 1));
	// 	}
	// 	else if (desc_ref.SetIndex == 1) {
	// 		desc_ref.pSet->Bind(1, cmd, pipeline.GetBindPoint(), pipeline);
	// 	}
	// }

	const Vec2u extent = gGraphics->GetWindow()->GetSize();

	// Combined matrix maps pixel space (top-left origin) into clip space.
	Mat4f projection;
	projection.LoadOrthographicMatrix(static_cast<float32>(extent.X), static_cast<float32>(extent.Y), 0.1f, 10.0f);

	TextPushConstants consts {};
	memcpy(consts.CombinedMatrix, projection.RawData, sizeof(consts.CombinedMatrix));
	consts.TextColor = uiColor;
	gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, consts);

	// Gather all glyph instances for this string.
	const float32 glyph_size = static_cast<float32>(scGlyphTexels) * scale;

	StackArray<InstanceData, scMaxGlyphs> instances;

	const float32 atlas_w = static_cast<float32>(mpAtlas->Info.Size.X);
	const float32 atlas_h = static_cast<float32>(mpAtlas->Info.Size.Y);

	Vec2f cursor = position;

	for (const char* c = text; *c != '\0' && instances.Size < scMaxGlyphs; ++c) {
		const int glyph_index = FindGlyphIndex(*c);

		// Unknown characters (including space) only advance the cursor.
		if (glyph_index < 0) {
			cursor.X += glyph_size;
			continue;
		}

		const uint32 col = static_cast<uint32>(glyph_index % scAtlasColumns);
		const uint32 row = static_cast<uint32>(glyph_index / scAtlasColumns);

		InstanceData& inst = *instances.Insert();
		inst.vPosition[0] = cursor.X;
		inst.vPosition[1] = cursor.Y;
		inst.vSize[0] = glyph_size;
		inst.vSize[1] = glyph_size;
		inst.vUvMin[0] = (static_cast<float32>(col) * scGlyphTexels) / atlas_w;
		inst.vUvMin[1] = (static_cast<float32>(row) * scGlyphTexels) / atlas_h;
		inst.vUvMax[0] = (static_cast<float32>(col + 1) * scGlyphTexels) / atlas_w;
		inst.vUvMax[1] = (static_cast<float32>(row + 1) * scGlyphTexels) / atlas_h;

		cursor.X += glyph_size;
	}

	if (instances.Size == 0) {
		return;
	}

	uint8* mapped = reinterpret_cast<uint8*>(mInstanceBuffer.pMappedBuffer);
	memcpy(mapped + write_offset, instances.pData, instances.Size * sizeof(InstanceData));
	mInstancesWritten += instances.Size;

	// Bind descriptor sets. Set 0 uses a dynamic offset into the instance buffer for the
	// current frame (like the object buffer), set 1 is the font atlas.
	// for (const Pipeline::DescriptorRef& desc_ref : pipeline.DescriptorIDs) {
	// 	if (desc_ref.SetIndex == 0) {
	// 		const uint32 offsets[] = { base_offset };
	// 		desc_ref.pSet->Bind(0, cmd, pipeline, Slice<const uint32>(offsets, 1));
	// 	}
	// 	else if (desc_ref.SetIndex == 1) {
	// 		desc_ref.pSet->Bind(1, cmd, pipeline.GetBindPoint(), pipeline);
	// 	}
	// }

	// Draw the glyph quad instanced (binds vertex + index buffers internally).
	mpQuad->Render(cmd, instances.Size);
}

} // namespace fx::renderer
