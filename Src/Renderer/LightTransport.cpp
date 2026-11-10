#include "LightTransport.hpp"

#include "Backend/BarrierHelper.hpp"
#include "Backend/DescriptorCache.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "GraphicsBackend.hpp"
#include "PSOBuild.hpp"
#include "PipelineCache.hpp"
#include "ShadowAtlas.hpp"

#include <Core/Log.hpp>
#include <Core/SizedArray.hpp>
#include <Map/CompiledMap.hpp>
#include <Renderer/Globals.hpp>
#include <Texture/TextureManager.hpp>
#include <cstring>

namespace fx::renderer {

namespace {

constexpr uint64 scPlaceholderBytes = 64;
constexpr uint16 scHalfOne = 0x3C00;
constexpr uint32 scNoChartPair = 0xFFFFFFFF;
constexpr uint32 scNoChart = 0xFFFF;

constexpr uint32 scInjectGroupSize = 64;
constexpr uint32 scApplyGroupSize = 8;

static_assert(sizeof(CompiledMapTransportChart) == 32);
static_assert(sizeof(CompiledMapTransportCluster) == 32);
static_assert(sizeof(CompiledMapTransportSample) == 32);
static_assert(sizeof(CompiledMapTransportLink) == 8);
static_assert(sizeof(LightTransportConsts) == 48);

/// The transport buffers have a single page, so their dynamic offsets are always 0
void AddZeroOffsets(uint32 count)
{
	for (uint32 i = 0; i < count; i++) {
		gPipelineCache->AddBufferOffset(0, 0);
	}
}

} // namespace

void LightTransport::CreateBuffer(RawGpuBuffer& buffer, uint64 size, const void* data)
{
	buffer.Destroy();

	const uint64 buffer_size = std::max(size, scPlaceholderBytes);

	buffer.Create(eGpuBufferType::Storage, buffer_size, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
				  eGpuBufferFlags::PersistentMapped);

	uint8* mapped = static_cast<uint8*>(buffer.pMappedBuffer);

	memset(mapped, 0, buffer_size);

	if (data != nullptr && size > 0) {
		memcpy(mapped, data, size);
	}

	buffer.FlushToGpu(0, static_cast<uint32>(buffer_size));
}

void LightTransport::Create()
{
	CreateBuffer(mCharts, 0, nullptr);
	CreateBuffer(mClusters, 0, nullptr);
	CreateBuffer(mSamples, 0, nullptr);
	CreateBuffer(mLinks, 0, nullptr);
	CreateBuffer(mTexelCharts, 0, nullptr);
	CreateBuffer(mState, 0, nullptr);

	const uint16 black[4] = { 0, 0, 0, scHalfOne };

	mpBakedIrradiance = gGraphics->CreateLightmapImage(
		Vec2u(1, 1), eImageFormat::RGBA16_Float, Slice<const uint8>(reinterpret_cast<const uint8*>(black), sizeof(black)));

	mpRelitIrradiance = gTextureManager->NewTexture();
	mpRelitIrradiance->Create(eImageType::Flat, Vec2u(1, 1), 1, eImageFormat::RGBA16_Float,
							  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);
}

void LightTransport::CreatePipelines()
{
	Sampler* point_sampler = gSamplerCache->Request(SamplerProps {
		eSamplerFilter::Nearest,
		eSamplerFilter::Nearest,
		eSamplerFilter::Nearest,
		eSamplerAddressMode::ClampToEdge,
	});

	Sampler* shadow_sampler = gSamplerCache->Request({
		eSamplerFilter::Linear,
		eSamplerFilter::Linear,
		eSamplerFilter::Linear,
		eSamplerAddressMode::ClampToBorder,
		eSamplerBorderColor::FloatWhite,
		eSamplerCompareOp::Greater,
	});

	gPSOBuild->BeginPipeline(ePipelineName::LightTransportInject);
	gPSOBuild->SetShader(eShaderName::LightTransportInject, {});
	gPSOBuild->AddBuffer(0, 0, eShaderType::Compute, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
						 gGraphics->LightBuffer.PageSize);
	gPSOBuild->AddImageFromTarget(1, 0, eShaderType::Compute, gShadowAtlas->GetTarget(), shadow_sampler);
	gPSOBuild->AddBuffer(2, 0, eShaderType::Compute, &mClusters, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddBuffer(3, 0, eShaderType::Compute, &mSamples, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddBuffer(4, 0, eShaderType::Compute, &mState, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddFrameData(1);
	gPSOBuild->EndPipeline();

	gPSOBuild->BeginPipeline(ePipelineName::LightTransportBounce);
	gPSOBuild->SetShader(eShaderName::LightTransportBounce, {});
	gPSOBuild->AddBuffer(0, 0, eShaderType::Compute, &mClusters, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddBuffer(1, 0, eShaderType::Compute, &mLinks, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddBuffer(2, 0, eShaderType::Compute, &mState, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddFrameData(1);
	gPSOBuild->EndPipeline();

	gPSOBuild->BeginPipeline(ePipelineName::LightTransportApply);
	gPSOBuild->SetShader(eShaderName::LightTransportApply, {});
	gPSOBuild->AddImage(0, 0, eShaderType::Compute, mpBakedIrradiance, point_sampler);
	gPSOBuild->AddBuffer(1, 0, eShaderType::Compute, &mCharts, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddBuffer(2, 0, eShaderType::Compute, &mClusters, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddBuffer(3, 0, eShaderType::Compute, &mState, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddBuffer(4, 0, eShaderType::Compute, &mTexelCharts, 0, VK_WHOLE_SIZE);
	gPSOBuild->AddStorageImage(5, 0, eShaderType::Compute, mpRelitIrradiance);
	gPSOBuild->AddFrameData(1);
	gPSOBuild->EndPipeline();
}

void LightTransport::Destroy()
{
	mCharts.Destroy();
	mClusters.Destroy();
	mSamples.Destroy();
	mLinks.Destroy();
	mTexelCharts.Destroy();
	mState.Destroy();

	mbActive = false;
}

bool LightTransport::Upload(const CompiledMapTransport& transport, const CompiledMapLightmap& lightmap)
{
	if (transport.IsEmpty() || !lightmap.HasIndirect() || transport.ClusterSize == 0) {
		Clear();
		return false;
	}

	const Vec2u size(lightmap.Width, lightmap.Height);
	const uint64 texel_count = lightmap.GetTexelCount();

	SizedArray<uint32> texel_charts;
	texel_charts.InitSize((texel_count + 1) / 2, scNoChartPair);

	for (uint32 chart_index = 0; chart_index < transport.Charts.Size && chart_index < scNoChart; chart_index++) {
		const CompiledMapTransportChart& chart = transport.Charts[chart_index];

		for (uint32 y = chart.Min[1]; y < chart.Min[1] + chart.Size[1]; y++) {
			for (uint32 x = chart.Min[0]; x < chart.Min[0] + chart.Size[0]; x++) {
				const uint64 texel = static_cast<uint64>(y) * size.X + x;
				const uint32 shift = static_cast<uint32>(texel & 1) * 16;

				uint32& pair = texel_charts[texel / 2];
				pair = (pair & ~(0xFFFFU << shift)) | (chart_index << shift);
			}
		}
	}

	SizedArray<uint16> baked_texels;
	baked_texels.InitSize(texel_count * 4);

	for (uint64 texel = 0; texel < texel_count; texel++) {
		for (uint32 channel = 0; channel < 3; channel++) {
			baked_texels[texel * 4 + channel] = lightmap.Irradiance[texel * 3 + channel];
		}

		baked_texels[texel * 4 + 3] = scHalfOne;
	}

	gGraphics->GetDevice()->WaitForIdle();

	mClusterCount = static_cast<uint32>(transport.Clusters.Size);
	mClusterSize = transport.ClusterSize;
	mLinkCount = transport.Links.Size;
	mLightmapSize = size;
	mReadHalf = 0;
	mIdleFrames = 0;

	CreateBuffer(mCharts, transport.Charts.GetSizeInBytes(), transport.Charts.pData);
	CreateBuffer(mClusters, transport.Clusters.GetSizeInBytes(), transport.Clusters.pData);
	CreateBuffer(mSamples, transport.Samples.GetSizeInBytes(), transport.Samples.pData);
	CreateBuffer(mLinks, transport.Links.GetSizeInBytes(), transport.Links.pData);
	CreateBuffer(mTexelCharts, texel_charts.GetSizeInBytes(), texel_charts.pData);
	CreateBuffer(mState, static_cast<uint64>(mClusterCount) * 3 * sizeof(float32) * 4, nullptr);

	mpBakedIrradiance->ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	gGraphics->SubmitImmediateUploadCmd(
		[&](CommandBuffer& cmd)
		{
			ImageInfo info(size, eImageFormat::RGBA16_Float, 0, 1,
						   Slice<const uint8>(reinterpret_cast<const uint8*>(baked_texels.pData),
											  baked_texels.GetSizeInBytes()));

			mpBakedIrradiance->CreateFromData(cmd, info, eImageCreateFlags::None);
		});

	mpRelitIrradiance->Create(eImageType::Flat, size, 1, eImageFormat::RGBA16_Float,
							  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);
	mpRelitIrradiance->ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	// Every buffer and both images are new Vulkan objects behind the same pointers
	const RawGpuBuffer* buffers[] = { &mCharts, &mClusters, &mSamples, &mLinks, &mTexelCharts, &mState };
	Image* images[] = { mpBakedIrradiance, mpRelitIrradiance };

	gDescriptorCache->RebuildUsing(Slice<const RawGpuBuffer* const>(buffers, std::size(buffers)),
								   Slice<Image* const>(images, std::size(images)));

	mbActive = true;
	gGraphics->pRenderer->RebuildPersistentDescriptor();

	LogInfo(LC_RENDER, "Light transport: {} clusters, {} links ({:.1f} MB)", mClusterCount, mLinkCount,
			static_cast<double>(transport.Links.GetSizeInBytes()) / (1024.0 * 1024.0));

	return true;
}

void LightTransport::Clear()
{
	if (!mbActive) {
		return;
	}

	gGraphics->GetDevice()->WaitForIdle();

	mbActive = false;
	gGraphics->pRenderer->RebuildPersistentDescriptor();
}

Image* LightTransport::GetRelitIrradiance() const
{
	if (!mbActive) {
		return nullptr;
	}

	return mpRelitIrradiance;
}

void LightTransport::Render(CommandBuffer& cmd, uint32 first_light, uint32 light_count, bool show)
{
	if (!mbActive) {
		return;
	}

	// With no unbaked lights the relit image settles back to the baked one, so keep it as it is
	if (light_count > 0 || show != mbLastShow) {
		mIdleFrames = 0;
	}
	else if (mIdleFrames <= scIdleFramesBeforeSleep) {
		mIdleFrames++;
	}

	mbLastShow = show;

	if (mIdleFrames > scIdleFramesBeforeSleep) {
		return;
	}

	const uint32 read_offset = mClusterCount * (1 + mReadHalf);
	const uint32 write_offset = mClusterCount * (2 - mReadHalf);

	LightTransportConsts consts {
		.FirstLight = first_light,
		.LightCount = light_count,
		.ClusterCount = mClusterCount,
		.ClusterSize = mClusterSize,
		.ReadOffset = read_offset,
		.WriteOffset = write_offset,
		.Flags = show ? scFlagShow : 0U,
		.LightmapSize = { mLightmapSize.X, mLightmapSize.Y },
	};

	const uint32 consts_offset = gGraphics->FrameAlloc.Push(consts);

	// The spot shadow tiles were just rendered
	BarrierHelper::ComputeReadAfterAll(cmd);

	// Every buffer binding is dynamic, so each takes an offset in binding order
	gPipelineCache->AddBufferOffset(0, gGraphics->LightBuffer.GetBaseOffset());
	AddZeroOffsets(3);
	gPipelineCache->SetFrameData(consts_offset);
	gPipelineCache->Bind(ePipelineName::LightTransportInject, cmd);

	vkCmdDispatch(cmd.Get(), (mClusterCount + scInjectGroupSize - 1) / scInjectGroupSize, 1, 1);

	BarrierHelper::ComputeReadAfterAll(cmd);

	AddZeroOffsets(3);
	gPipelineCache->SetFrameData(consts_offset);
	gPipelineCache->Bind(ePipelineName::LightTransportBounce, cmd);

	const uint32 bounce_columns = std::min(mClusterCount, scBounceRow);
	const uint32 bounce_rows = (mClusterCount + scBounceRow - 1) / scBounceRow;

	vkCmdDispatch(cmd.Get(), bounce_columns, bounce_rows, 1);

	BarrierHelper::ComputeReadAfterAll(cmd);
	BarrierHelper::ImageBeginComputeWrite(cmd, mpRelitIrradiance);

	AddZeroOffsets(4);
	gPipelineCache->SetFrameData(consts_offset);
	gPipelineCache->Bind(ePipelineName::LightTransportApply, cmd);

	vkCmdDispatch(cmd.Get(), (mLightmapSize.X + scApplyGroupSize - 1) / scApplyGroupSize,
				  (mLightmapSize.Y + scApplyGroupSize - 1) / scApplyGroupSize, 1);

	BarrierHelper::ImageComputeWriteToRead(cmd, mpRelitIrradiance);

	mReadHalf ^= 1U;
}

} // namespace fx::renderer
