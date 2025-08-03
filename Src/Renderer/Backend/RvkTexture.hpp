#pragma once

#include <vulkan/vulkan.h>

#include <Core/FxSizedArray.hpp>

#include "RvkGpuBuffer.hpp"
#include "RvkImage.hpp"
#include "RvkDevice.hpp"
#include "RvkSampler.hpp"

#include "Fwd/Fwd_SubmitUploadGpuCmd.hpp"
// #include "Fwd/Fwd_AddToDeletionQueue.hpp"
#include "Fwd/Fwd_GetDevice.hpp"

#include <Math/Vec2.hpp>
#include <Core/FxRef.hpp>

#include <assert.h>

class RvkTexture
{
public:
    RvkTexture() = default;

    void Create(const FxSizedArray<uint8>& image_data, const FxVec2u& dimensions, VkFormat format, uint32 components, const FxRef<RvkSampler>& sampler)
    {
        Sampler = sampler;
        Create(image_data, dimensions, format, components);
    }

    // TODO: update this and remove the format/color restrictions
    void Create(const FxSizedArray<uint8>& image_data, const FxVec2u& dimensions, VkFormat format, uint32 components)
    {
        mDevice = Fx_Fwd_GetGpuDevice();

        // TODO: pass in stride for texture size
        size_t data_size = dimensions.X * dimensions.Y * components;
        assert(image_data.Size == data_size);

        RvkRawGpuBuffer<uint8> staging_buffer;
        staging_buffer.Create(data_size, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        staging_buffer.Upload(image_data);

        Image.Create(dimensions, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

        // Copy image data to a new image fully on the GPU
        Fx_Fwd_SubmitUploadCmd([&](RvkCommandBuffer& cmd) {
            Image.TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd);

            VkBufferImageCopy copy {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .imageExtent = VkExtent3D{
                    .width = dimensions.X,
                    .height = dimensions.Y,
                    .depth = 1,
                },
            };

            vkCmdCopyBufferToImage(cmd, staging_buffer.Buffer, Image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            Image.TransitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd);
        });
    }

    void SetSampler(const FxRef<RvkSampler>& sampler)
    {
        Sampler = sampler;
    }

    FxRef<RvkSampler> GetSampler()
    {
        return Sampler;
    }

    void Destroy()
    {
        this->Image.Destroy();
    }

    ~RvkTexture()
    {
        this->Destroy();
    }

public:
    RvkImage Image;
    FxRef<RvkSampler> Sampler{nullptr};

private:
    RvkGpuDevice* mDevice = nullptr;
};
