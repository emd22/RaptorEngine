#pragma once

#include "Backend/Commands.hpp"
#include "Backend/Device.hpp"
#include "Constants.hpp"

#include <vulkan/vulkan.h>

#include <Core/Log.hpp>
#include <Core/Types.hpp>
#include <vector>

namespace fx::renderer {


enum class eGpuMarker : uint32
{
	FrameStart,
	Shadows,
	Prepass,
	LightCulling,
	SSAO,
	Forward,
	ProbeCapture,
	Composition,

	Count,
};

class GpuProfiler
{
public:
	static constexpr uint32 scNumMarkers = static_cast<uint32>(eGpuMarker::Count);

	/// How long timings are averaged over before they are shown
	static constexpr double scWindowSeconds = 0.25;

public:
	void Create(GpuDevice* device, uint32 graphics_family)
	{
		mDevice = device;

		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties(device->Physical, &properties);

		uint32 family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device->Physical, &family_count, nullptr);

		std::vector<VkQueueFamilyProperties> families(family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(device->Physical, &family_count, families.data());

		const uint32 valid_bits = (graphics_family < family_count) ? families[graphics_family].timestampValidBits : 0;

		if (properties.limits.timestampComputeAndGraphics == VK_FALSE || valid_bits == 0 ||
			properties.limits.timestampPeriod <= 0.0f) {
			LogWarning(LC_RENDER,
					   "GPU timestamps are not supported (compute and graphics={}, valid bits={}, period={})",
					   properties.limits.timestampComputeAndGraphics, valid_bits, properties.limits.timestampPeriod);
			return;
		}

		// The period is nanoseconds per tick
		mMsPerTick = static_cast<double>(properties.limits.timestampPeriod) * 1e-6;
		mValidMask = (valid_bits >= 64) ? ~0ULL : ((1ULL << valid_bits) - 1ULL);

		const VkQueryPoolCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
			.queryType = VK_QUERY_TYPE_TIMESTAMP,
			.queryCount = scNumMarkers,
		};

		for (uint32 i = 0; i < FramesInFlight; i++) {
			if (vkCreateQueryPool(device->Device, &create_info, nullptr, &mPools[i]) != VK_SUCCESS) {
				LogWarning(LC_RENDER, "Could not create a timestamp query pool, GPU timings are off");
				Destroy();
				return;
			}
		}

		mbEnabled = true;

		LogInfo(LC_RENDER, "GPU timestamps are on ({} valid bits, {} ns per tick)", valid_bits,
				properties.limits.timestampPeriod);
	}

	void Destroy()
	{
		for (VkQueryPool& pool : mPools) {
			if (pool != VK_NULL_HANDLE) {
				vkDestroyQueryPool(mDevice->Device, pool, nullptr);
				pool = VK_NULL_HANDLE;
			}
		}

		mbEnabled = false;
	}

	bool IsEnabled() const { return mbEnabled; }

	/**
	 * @brief Reads back what the frame slot's last use timed and folds it into the averages. Call once the slot's fence
	 * has been waited on, before the slot is recorded again.
	 */
	void ReadResults(uint32 frame_index, double delta_seconds)
	{
		if (!mbEnabled || mWritten[frame_index] == 0) {
			return;
		}

		// Value and availability for each marker
		uint64 results[scNumMarkers * 2] = {};

		const VkResult status = vkGetQueryPoolResults(mDevice->Device, mPools[frame_index], 0, scNumMarkers,
													  sizeof(results), results, sizeof(uint64) * 2,
													  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

		// Not ready, or nothing in it: the frame was never submitted
		if (status != VK_SUCCESS && status != VK_NOT_READY) {
			return;
		}

		double stage_ms[scNumMarkers] = {};
		int32 previous = -1;

		for (uint32 marker = 0; marker < scNumMarkers; marker++) {
			if ((mWritten[frame_index] & (1U << marker)) == 0) {
				continue;
			}

			// One of the frame's markers did not land, so the frame is not usable
			if (results[marker * 2 + 1] == 0) {
				return;
			}

			if (previous >= 0) {
				const uint64 ticks = (results[marker * 2] - results[previous * 2]) & mValidMask;
				stage_ms[marker] = static_cast<double>(ticks) * mMsPerTick;
			}

			previous = static_cast<int32>(marker);
		}

		for (uint32 marker = 0; marker < scNumMarkers; marker++) {
			mWindowSums[marker] += stage_ms[marker];
		}

		mWindowFrames++;
		mWindowTime += delta_seconds;

		if (mWindowTime >= scWindowSeconds && mWindowFrames > 0) {
			for (uint32 marker = 0; marker < scNumMarkers; marker++) {
				mAverages[marker] = mWindowSums[marker] / static_cast<double>(mWindowFrames);
				mWindowSums[marker] = 0.0;
			}

			mWindowFrames = 0;
			mWindowTime = 0.0;
		}
	}

	void BeginFrame(const CommandBuffer& cmd, uint32 frame_index)
	{
		if (!mbEnabled) {
			return;
		}

		mCurrentFrame = frame_index;
		mWritten[frame_index] = 0;

		vkCmdResetQueryPool(cmd.Get(), mPools[frame_index], 0, scNumMarkers);

		Mark(cmd, eGpuMarker::FrameStart);
	}

	void Mark(const CommandBuffer& cmd, eGpuMarker marker)
	{
		if (!mbEnabled) {
			return;
		}

		const uint32 index = static_cast<uint32>(marker);

		vkCmdWriteTimestamp(cmd.Get(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mPools[mCurrentFrame], index);

		mWritten[mCurrentFrame] |= (1U << index);
	}

	double GetMs(eGpuMarker marker) const { return mAverages[static_cast<uint32>(marker)]; }

	double GetTotalMs() const
	{
		double total = 0.0;

		for (const double ms : mAverages) {
			total += ms;
		}

		return total;
	}

private:
	GpuDevice* mDevice = nullptr;
	bool mbEnabled = false;

	VkQueryPool mPools[FramesInFlight] = {};
	uint32 mWritten[FramesInFlight] = {};
	uint32 mCurrentFrame = 0;

	double mMsPerTick = 0.0;
	uint64 mValidMask = ~0ULL;

	double mWindowSums[scNumMarkers] = {};
	double mWindowTime = 0.0;
	uint32 mWindowFrames = 0;

	double mAverages[scNumMarkers] = {};
};

} // namespace fx::renderer
