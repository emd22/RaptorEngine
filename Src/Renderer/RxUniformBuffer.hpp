#pragma once

#include "Backend/RxGpuBuffer.hpp"
#include "Backend/RxShader.hpp"

#include <Core/FxTypes.hpp>


class RxUniforms
{
    static constexpr uint32 scUniformBufferSize = 512;

public:
    RxUniforms() = default;

    void Create();

    /**
     * @brief Resets the uniform buffer to overwrite values from the previous frame(s).
     */
    void Rewind();

public:
    /// Gpu buffer that stores current uniform buffer data. Note that this is a continguous buffer that stores
    /// `RxFramesInFlight` number of uniform structures that are of size `scUniformBufferSize`.
    RxRawGpuBuffer<uint32> mGpuBuffer;
};
