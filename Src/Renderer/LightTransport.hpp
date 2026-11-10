/*
 * File:        LightTransport.hpp
 * Author:      emd22
 * Created:     05/11/2026
 * Description: Bounces the light of lights that were not baked (persistent lights, like the flashlight) through the
 *              cluster links that mapc stores in the compiled map, and adds it to the baked lightmap irradiance.
 */

#pragma once

#include "Backend/GpuBuffer.hpp"

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>

namespace fx {
class Image;
struct CompiledMapLightmap;
struct CompiledMapTransport;
} // namespace fx

namespace fx::renderer {

class CommandBuffer;

/// Laid out to match LightTransportConsts in Shaders/LightTransportCommon.hlsli
struct LightTransportConsts
{
	uint32 FirstLight = 0;
	uint32 LightCount = 0;
	uint32 ClusterCount = 0;
	uint32 ClusterSize = 0;

	uint32 ReadOffset = 0;
	uint32 WriteOffset = 0;
	uint32 Flags = 0;
	uint32 Padding = 0;

	uint32 LightmapSize[2] = {};
	uint32 Padding2[2] = {};
};

class LightTransport
{
public:
	static constexpr uint32 scFlagShow = 0x1;
	static constexpr uint32 scBounceRow = 1024;
	/// Frames without unbaked lights before the passes stop, by when the last bounce has faded out
	static constexpr uint32 scIdleFramesBeforeSleep = 32;

public:
	/// Makes placeholder buffers and images, so the pipelines can be built before a map is loaded
	void Create();
	void CreatePipelines();
	void Destroy();

	/**
	 * @brief Takes the compiled map's links and baked irradiance. Waits for the GPU to be idle.
	 * @returns false if the map has no transport, which leaves relighting off
	 */
	bool Upload(const CompiledMapTransport& transport, const CompiledMapLightmap& lightmap);

	/// Turns relighting off, binding the baked irradiance for shading again
	void Clear();

	/**
	 * @brief Records the inject, bounce and apply passes. Lights in light buffer slots [first_light, first_light +
	 * light_count) are the ones that bounce.
	 * @param show False to write only the baked irradiance, for probe bakes
	 */
	void Render(CommandBuffer& cmd, uint32 first_light, uint32 light_count, bool show);

	bool IsActive() const { return mbActive; }

	/// The irradiance image the forward pass samples: the relit one while relighting is on, else nullptr
	Image* GetRelitIrradiance() const;

	uint32 GetClusterCount() const { return mClusterCount; }
	uint64 GetLinkCount() const { return mLinkCount; }

private:
	void CreateBuffer(RawGpuBuffer& buffer, uint64 size, const void* data);

private:
	RawGpuBuffer mCharts;
	RawGpuBuffer mClusters;
	RawGpuBuffer mSamples;
	RawGpuBuffer mLinks;
	RawGpuBuffer mTexelCharts;
	/// Exitance of every cluster, then two copies of the bounced irradiance that swap every frame
	RawGpuBuffer mState;

	Image* mpBakedIrradiance = nullptr;
	Image* mpRelitIrradiance = nullptr;

	Vec2u mLightmapSize = Vec2u(1, 1);
	uint32 mClusterSize = 1;
	uint32 mClusterCount = 0;
	uint64 mLinkCount = 0;

	/// Which half of the state buffer holds last frame's bounce
	uint32 mReadHalf = 0;

	uint32 mIdleFrames = 0;
	bool mbLastShow = true;

	bool mbActive = false;
};

} // namespace fx::renderer
