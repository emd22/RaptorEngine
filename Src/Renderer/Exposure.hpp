#pragma once

#include <Core/Types.hpp>

#include <algorithm>
#include <cmath>

namespace fx {

struct ExposureSettings
{
    static constexpr float32 scMinValue = 1e-4f;
    static constexpr float32 scMeterCalibration = 1.2f;

    float32 Aperture = 16.0f;
    float32 ShutterTime = 0.01f;
    float32 ISO = 100.0f;
    float32 Compensation = 0.0f;

    float32 GetEV100() const
    {
        const float32 aperture = std::max(Aperture, scMinValue);
        const float32 shutter = std::max(ShutterTime, scMinValue);
        const float32 iso = std::max(ISO, scMinValue);

        return std::log2((aperture * aperture / shutter) * (100.0f / iso));
    }

    float32 GetExposure() const
    {
        return std::exp2(Compensation) / (scMeterCalibration * std::exp2(GetEV100()));
    }
};

} // namespace fx
