module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

module TextureManager;

import vulkan;
import vk_mem_alloc;
import Logger;
import VkUtil;
import AssetData;

namespace {

constexpr vk::DeviceSize TEXTURE_UPLOAD_ALIGNMENT = 16;

/// @return level n of an image that size, down to 1x1.
vk::Extent2D levelExtent(vk::Extent2D extent, uint32_t level) {
    return vk::Extent2D{std::max(extent.width >> level, 1u),
                        std::max(extent.height >> level, 1u)};
}

vk::ImageSubresourceLayers colorLayer(uint32_t mipLevel) {
    return vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, mipLevel, 0, 1};
}

} // namespace

TextureManager::~TextureManager() {
    if (device_) logError("TextureManager: destroy() was not called before destruction");
}

bool TextureManager::init(VulkanContext& ctx) {
    if (device_) {
        logError("TextureManager: init called twice");
        return false;
    }
    device_    = ctx.device();
    allocator_ = ctx.allocator();

    vk::PhysicalDeviceDescriptorIndexingProperties IndexingProperties{};
    vk::PhysicalDeviceProperties2 properties{};
    properties.pNext = &IndexingProperties;
    ctx.physicalDevice().getProperties2(&properties);

    /* A set may exceed neither allowance. */
    capacity_ = std::min(IndexingProperties.maxDescriptorSetUpdateAfterBindSampledImages,
                         IndexingProperties.maxPerStageDescriptorUpdateAfterBindSampledImages);
    if (capacity_ == 0) {
        logError("TextureManager: the device allows no update-after-bind sampled images");
        return false;
    }
    logMessage("TextureManager: " + std::to_string(capacity_) + " texture slots");

    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter    = vk::Filter::eLinear;
    samplerInfo.minFilter    = vk::Filter::eLinear;
    samplerInfo.mipmapMode   = vk::SamplerMipmapMode::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
    samplerInfo.maxLod       = vk::LodClampNone;
    if (device_.createSampler(&samplerInfo, nullptr, &sampler_) != vk::Result::eSuccess) {
        logError("TextureManager: vkCreateSampler failed");
        destroy();
        return false;
    }
//TODO: more fixed samplers determined per texture at load time
    vk::DescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    binding.descriptorCount = capacity_;
    binding.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    /* Partially bound: the array is declared at the device limit and only the
     * slots handed out are written. */
    const vk::DescriptorBindingFlags bindingFlags =
        vk::DescriptorBindingFlagBits::ePartiallyBound |
        vk::DescriptorBindingFlagBits::eUpdateAfterBind;
    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.bindingCount  = 1;
    flagsInfo.pBindingFlags = &bindingFlags;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings    = &binding;
    layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
    layoutInfo.pNext = &flagsInfo;
    if (device_.createDescriptorSetLayout(&layoutInfo, nullptr, &layout_) !=
        vk::Result::eSuccess) {
        logError("TextureManager: vkCreateDescriptorSetLayout failed");
        destroy();
        return false;
    }

    const vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler,
                                          capacity_};
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
    if (device_.createDescriptorPool(&poolInfo, nullptr, &pool_) != vk::Result::eSuccess) {
        logError("TextureManager: vkCreateDescriptorPool failed");
        destroy();
        return false;
    }

    vk::DescriptorSetAllocateInfo setInfo{};
    setInfo.descriptorPool     = pool_;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts        = &layout_;
    if (device_.allocateDescriptorSets(&setInfo, &set_) != vk::Result::eSuccess) {
        logError("TextureManager: vkAllocateDescriptorSets failed");
        destroy();
        return false;
    }
    return true;
}

uint32_t TextureManager::add(BufferManager& buffers, vk::CommandBuffer commandBuffer,
                             std::span<const std::byte> levels, vk::Format format,
                             vk::Extent2D extent, uint32_t levelCount) {
    if (!device_ || !commandBuffer) {
        logError("TextureManager::add: called before init");
        return INVALID_TEXTURE_INDEX;
    }
    if (levels.empty() || extent.width == 0 || extent.height == 0 || levelCount == 0) {
        logError("TextureManager::add: empty texture");
        return INVALID_TEXTURE_INDEX;
    }
    const AssetFormat assetFormat{static_cast<uint32_t>(format)};
    if (assetLevelBytes(assetFormat, 1, 1) == 0) {
        logError("TextureManager::add: format " + vk::to_string(format) +
                 " has no known level size");
        return INVALID_TEXTURE_INDEX;
    }
    if (images_.size() >= capacity_) {
        logError("TextureManager::add: the bindless array holds " +
                 std::to_string(capacity_) + " textures and is full");
        return INVALID_TEXTURE_INDEX;
    }

    /* One region per level, over the levels laid out largest first. */
    std::vector<vk::BufferImageCopy2> regions;
    regions.reserve(levelCount);
    vk::DeviceSize levelOffset = 0;
    for (uint32_t level = 0; level < levelCount; ++level) {
        const vk::Extent2D   size  = levelExtent(extent, level);
        const vk::DeviceSize bytes =
            assetLevelBytes(assetFormat, size.width, size.height);
        if (levelOffset + bytes > levels.size()) {
            logError("TextureManager::add: the pixels hold " +
                     std::to_string(levels.size()) + " bytes, short of the " +
                     std::to_string(levelCount) + " levels claimed");
            return INVALID_TEXTURE_INDEX;
        }

        vk::BufferImageCopy2 region{};
        region.bufferOffset     = levelOffset;
        region.imageSubresource = colorLayer(level);
        region.imageExtent      = vk::Extent3D{size.width, size.height, 1};
        regions.push_back(region);

        levelOffset += bytes;
    }

    const MappedSpan<std::byte> upload =
        buffers.allocateUpload(levels.size(), TEXTURE_UPLOAD_ALIGNMENT);
    if (!upload) {
        logError("TextureManager::add: no upload space for " +
                 std::to_string(levels.size()) + " bytes");
        return INVALID_TEXTURE_INDEX;
    }
    std::memcpy(upload.host, levels.data(), levels.size());
    for (vk::BufferImageCopy2& region : regions)
        region.bufferOffset += upload.region.offset;

    Image& image = images_.emplace_back();
    if (!image.allocate(allocator_, vk::ImageType::e2D, format,
                        vk::Extent3D{extent.width, extent.height, 1},
                        vk::ImageUsageFlagBits::eSampled |
                            vk::ImageUsageFlagBits::eTransferDst,
                        levelCount)) {
        images_.pop_back();
        return INVALID_TEXTURE_INDEX;
    }

    transitionImage(commandBuffer, image.handle(), vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::PipelineStageFlagBits2::eTopOfPipe, {},
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::ImageAspectFlagBits::eColor, 0, levelCount);

    vk::CopyBufferToImageInfo2 copyInfo{};
    copyInfo.srcBuffer      = upload.region.buffer;
    copyInfo.dstImage       = image.handle();
    copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
    copyInfo.regionCount    = static_cast<uint32_t>(regions.size());
    copyInfo.pRegions       = regions.data();
    commandBuffer.copyBufferToImage2(copyInfo);

    transitionImage(commandBuffer, image.handle(),
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eFragmentShader,
                    vk::AccessFlagBits2::eShaderSampledRead,
                    vk::ImageAspectFlagBits::eColor, 0, levelCount);

    const ImageRef view = image.view(
        vk::ImageViewType::e2D, format,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, levelCount, 0, 1});
    if (!view) {
        images_.pop_back();
        return INVALID_TEXTURE_INDEX;
    }

    const uint32_t slot = static_cast<uint32_t>(images_.size() - 1);

    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler     = sampler_;
    imageInfo.imageView   = view.get();
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::WriteDescriptorSet write{};
    write.dstSet          = set_;
    write.dstBinding      = 0;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo      = &imageInfo;
    device_.updateDescriptorSets(1, &write, 0, nullptr);

    return slot;
}

void TextureManager::destroy() {
    if (!device_) return;

    for (Image& image : images_) image.destroy();
    images_.clear();

    device_.destroyDescriptorPool(pool_);
    device_.destroyDescriptorSetLayout(layout_);
    device_.destroySampler(sampler_);

    set_       = nullptr;
    pool_      = nullptr;
    layout_    = nullptr;
    sampler_   = nullptr;
    allocator_ = nullptr;
    device_    = nullptr;
}
