#pragma once

#include <Core/FxStaticArray.hpp>
#include "RvkGpuBuffer.hpp"
#include "Fwd/Fwd_SubmitUploadGpuCmd.hpp"
#include "Fwd/Fwd_AddToDeletionQueue.hpp"
#include "Fwd/Fwd_GetDevice.hpp"

#include <Math/Vec2.hpp>

#include "RvkImage.hpp"
#include <vulkan/vulkan.h>

#include "RvkDevice.hpp"
#include "vulkan/vulkan_core.h"

#include <assert.h>

namespace vulkan {

class RvkTexture
{
public:
    RvkTexture() = default;

    void Create(const FxStaticArray<uint8>& image_data, Vec2u dimensions, uint32 components)
    {
        mDevice = Fx_Fwd_GetGpuDevice();

        size_t data_size = dimensions.X * dimensions.Y * components;
        assert(image_data.Size == data_size);

        RvkRawGpuBuffer<uint8> staging_buffer;
        staging_buffer.Create(data_size, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        staging_buffer.Upload(image_data);

        Image.Create(dimensions, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

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

        VkSamplerCreateInfo sampler_info{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,

            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,

            .anisotropyEnable = VK_FALSE,
            // .maxAnisotropy = 0,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,

            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,

            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .mipLodBias = 0.0f,
            .minLod = 0.0f,
            .maxLod = 0.0f,
        };

        VkResult result = vkCreateSampler(mDevice->Device, &sampler_info, nullptr, &Sampler);
        if (result != VK_SUCCESS) {
            Log::Error("Error creating texture sampler!", 0);

            Image.Destroy();
            Sampler = nullptr;
        }
    }


    void Destroy()
    {

        if (this->Sampler != nullptr) {
            vkDestroySampler(this->mDevice->Device, this->Sampler, nullptr);
            this->Sampler = nullptr;
        }

        this->Image.Destroy();
        // Fx_Fwd_AddToDeletionQueue([this](FxDeletionObject* obj) {
        //     if (this->Sampler != nullptr) {
        //         printf("Destroy sampler!!\n");

        //         vkDestroySampler(this->mDevice->Device, this->Sampler, nullptr);
        //         this->Sampler = nullptr;
        //     }

        //     printf("Destroy image!!\n");

        //     this->Image.Destroy();
        // });
    }

    ~RvkTexture()
    {
        this->Destroy();
    }

public:
    RvkImage Image;
    VkSampler Sampler = nullptr;

private:
    RvkGpuDevice* mDevice = nullptr;
};

};
