#pragma once

#include "Commands.hpp"
#include "Descriptors.hpp"
#include "Synchro.hpp"

#include <Math/FMat4.hpp>

namespace fx::renderer {

struct alignas(16) UniformBufferObject
{
	FMat4 MvpMatrix;
};

struct FrameData
{
public:
	void Create(GpuDevice* device);
	void Destroy();

public:
	CommandPool CmdPool;
	CommandBuffer CmdBuffer;

	Semaphore ImageAvailable;
	Semaphore RenderFinished;
	Fence InFlight;
};

} // namespace fx::renderer
