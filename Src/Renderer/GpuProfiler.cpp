#include "GpuProfiler.hpp"

namespace fx::renderer {

void GpuProfiler::Create(GpuDevice* device, uint32 graphics_family)
{
	Destroy();

	AssertEqual(rx_gpu_marker_count(), scNumMarkers);

	mDevice = device;
	mpProfiler = rx_gpu_profiler_new(device->GetRustDevice(), graphics_family, FramesInFlight);
}

void GpuProfiler::Destroy()
{
	if (mpProfiler == nullptr) {
		return;
	}

	rx_gpu_profiler_destroy(mpProfiler, mDevice->GetRustDevice());
	mpProfiler = nullptr;
}

void GpuProfiler::ReadResults(uint32 frame_index, double delta_seconds)
{
	if (mpProfiler != nullptr) {
		rx_gpu_profiler_read_results(mpProfiler, mDevice->GetRustDevice(), frame_index, delta_seconds);
	}
}

void GpuProfiler::BeginFrame(const CommandBuffer& cmd, uint32 frame_index)
{
	if (mpProfiler != nullptr) {
		rx_gpu_profiler_begin_frame(mpProfiler, mDevice->GetRustDevice(), cmd.Cmd, frame_index);
	}
}

void GpuProfiler::Mark(const CommandBuffer& cmd, eGpuMarker marker)
{
	if (mpProfiler != nullptr) {
		rx_gpu_profiler_mark(mpProfiler, mDevice->GetRustDevice(), cmd.Cmd, static_cast<uint32>(marker));
	}
}

double GpuProfiler::GetMs(eGpuMarker marker) const
{
	return (mpProfiler != nullptr) ? rx_gpu_profiler_average_ms(mpProfiler, static_cast<uint32>(marker)) : 0.0;
}

double GpuProfiler::GetTotalMs() const
{
	return (mpProfiler != nullptr) ? rx_gpu_profiler_total_ms(mpProfiler) : 0.0;
}

} // namespace fx::renderer
