#pragma once

#include <raptor_ffi.h>
#include <vulkan/vulkan.h>

#include <Core/LockContext.hpp>
#include <Core/Types.hpp>
#include <atomic>
#include <cstdint>
#include <type_traits>

// #define FX_DEBUG_DEVICE_ASSERT_INITIALIZED 1

namespace fx::renderer {

template <typename THandle>
inline uint64 RxRaw(THandle handle)
{
    if constexpr (std::is_pointer_v<THandle>) {
        return reinterpret_cast<uint64>(handle);
    }
    else {
        return static_cast<uint64>(handle);
    }
}

template <typename THandle>
inline THandle RxFromRaw(uint64 raw)
{
    if constexpr (std::is_pointer_v<THandle>) {
        return reinterpret_cast<THandle>(raw);
    }
    else {
        return static_cast<THandle>(raw);
    }
}

class QueueFamilies
{
public:
    static const uint32 scNullQueue = UINT32_MAX;

    void Set(uint32 graphics, uint32 present, uint32 transfer)
    {
        mGraphicsIndex = graphics;
        mPresentIndex = present;
        mTransferIndex = transfer;
    }

    /// Checks if the main graphics and presentation queues have been queried
    bool IsComplete()
    {
        return (mGraphicsIndex != scNullQueue && mPresentIndex != scNullQueue && mTransferIndex != scNullQueue);
    }

    FX_FORCE_INLINE bool HasIndependentTransfer() const { return mTransferIndex != mGraphicsIndex; }

    uint32 GetGraphicsFamily() const { return mGraphicsIndex; }
    uint32 GetPresentFamily() const { return mPresentIndex; }
    uint32 GetTransferFamily() const { return mTransferIndex; }

private:
    uint32 mGraphicsIndex = scNullQueue;
    uint32 mPresentIndex = scNullQueue;
    uint32 mTransferIndex = scNullQueue;
};

class GpuDevice
{
public:
    GpuDevice() = default;

    void Create(RxGpuInstance* instance, VkSurfaceKHR surface);
    void Destroy();

    void WaitForIdle();

    RxGpuDevice* GetRustDevice() const { return mpRustDevice; }

    VkSurfaceFormatKHR GetSurfaceFormat();

    operator VkDevice() const { return Device; }
    operator VkPhysicalDevice() const { return Physical; }

    ~GpuDevice();

public:
    VkPhysicalDevice Physical = nullptr;
    VkDevice Device = nullptr;

    QueueFamilies mQueueFamilies;

    float32 MaxSamplerAnisotropy = 1.0f;
    bool bSupportsCubeArrays = false;

private:
    RxGpuDevice* mpRustDevice = nullptr;

};

} // namespace fx::renderer
