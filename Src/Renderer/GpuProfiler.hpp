#pragma once

#include "Backend/Commands.hpp"
#include "Backend/Device.hpp"
#include "Constants.hpp"

#include <vulkan/vulkan.h>

#include <Core/Types.hpp>

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

/**
 * @brief Times the stages of the GPU frame with timestamp queries. The queries and the averaging live in Rust, this is
 * the way to reach them. It does nothing if the GPU can not take timestamps.
 */
class GpuProfiler
{
public:
	static constexpr uint32 scNumMarkers = static_cast<uint32>(eGpuMarker::Count);

public:
	GpuProfiler() = default;
	GpuProfiler(const GpuProfiler&) = delete;
	GpuProfiler& operator=(const GpuProfiler&) = delete;

	void Create(GpuDevice* device, uint32 graphics_family);
	void Destroy();

	bool IsEnabled() const { return mpProfiler != nullptr; }

	/**
	 * @brief Reads back what the frame slot's last use timed and folds it into the averages. Call once the slot's fence
	 * has been waited on, before the slot is recorded again.
	 */
	void ReadResults(uint32 frame_index, double delta_seconds);

	void BeginFrame(const CommandBuffer& cmd, uint32 frame_index);

	/// Writes a timestamp for the end of a stage into the frame being recorded
	void Mark(const CommandBuffer& cmd, eGpuMarker marker);

	double GetMs(eGpuMarker marker) const;
	double GetTotalMs() const;

	~GpuProfiler() { Destroy(); }

private:
	GpuDevice* mDevice = nullptr;
	RxGpuProfiler* mpProfiler = nullptr;
};

} // namespace fx::renderer
