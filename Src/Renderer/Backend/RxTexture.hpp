#pragma once

#include "Fwd/Fwd_SubmitUploadGpuCmd.hpp"
#include "RxDevice.hpp"
#include "RxGpuBuffer.hpp"
#include "RxImage.hpp"
#include "RxSampler.hpp"

#include <vulkan/vulkan.h>

#include <Core/FxSizedArray.hpp>
// #include "Fwd/Fwd_AddToDeletionQueue.hpp"
#include "Fwd/Fwd_GetDevice.hpp"

#include <assert.h>

#include <Core/FxRef.hpp>
#include <Math/FxVec2.hpp>

class RxTexture
{
public:
    RxTexture() = default;

    void Create(RxImageType image_type, const FxSizedArray<uint8>& image_data, const FxVec2u& dimensions, VkFormat format, uint32 components,
                const FxRef<RxSampler>& sampler)
    {
        Sampler = sampler;
        Create(image_type, image_data, dimensions, format, components);
    }

    // TODO: update this and remove the format/color restrictions
    void Create(RxImageType image_type, const FxSizedArray<uint8>& image_data, const FxVec2u dimensions, VkFormat format, uint32 components)
    {
        mDevice = Fx_Fwd_GetGpuDevice();

        // TODO: pass in stride for texture size
        const size_t data_size = dimensions.X * dimensions.Y * components;
        //        assert(image_data.Size == data_size);

        RxRawGpuBuffer<uint8> staging_buffer;
        staging_buffer.Create(data_size, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        staging_buffer.Upload(image_data);

        const VkImageUsageFlags usage_flags = (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        Image.Create(image_type, dimensions, format, VK_IMAGE_TILING_OPTIMAL, usage_flags, VK_IMAGE_ASPECT_COLOR_BIT);

        RxImageTypeProperties type_properties = RxImageTypeGetProperties(image_type);

        // Copy image data to a new image fully on the GPU
        Fx_Fwd_SubmitUploadCmd(
            [&](RxCommandBuffer& cmd)
            {
                Image.TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd);

                VkBufferImageCopy copy {
                    .bufferOffset = 0,
                    .bufferRowLength = 0,
                    .bufferImageHeight = 0,
                    .imageSubresource {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = type_properties.LayerCount,
                    },
                    .imageExtent =
                        VkExtent3D {
                            .width = dimensions.X,
                            .height = dimensions.Y,
                            .depth = 1,
                        },
                };

                vkCmdCopyBufferToImage(cmd, staging_buffer.Buffer, Image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

                Image.TransitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd);
            });
    }

    void SetSampler(const FxRef<RxSampler>& sampler)
    {
        Sampler = sampler;
    }

    FxRef<RxSampler> GetSampler()
    {
        return Sampler;
    }

    void Destroy()
    {
        this->Image.Destroy();
    }

    ~RxTexture()
    {
        this->Destroy();
    }

public:
    RxImage Image;
    FxRef<RxSampler> Sampler { nullptr };

private:
    RxGpuDevice* mDevice = nullptr;
};
