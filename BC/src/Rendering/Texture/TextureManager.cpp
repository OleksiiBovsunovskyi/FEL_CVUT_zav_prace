module;

#include <algorithm>
#include <array>
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

constexpr uint32_t SAMPLED_BINDING = 0;
constexpr uint32_t STORAGE_BINDING = 1;

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

    storageCapacity_ = std::min({MAX_STORAGE_IMAGES,
                                 IndexingProperties.maxDescriptorSetUpdateAfterBindStorageImages,
                                 IndexingProperties.maxPerStageDescriptorUpdateAfterBindStorageImages});
    /* Compute sees both arrays, so together they stay within one stage's resources. */
    const uint32_t stageResources = IndexingProperties.maxPerStageUpdateAfterBindResources;
    imageCapacity_ = std::min({IndexingProperties.maxDescriptorSetUpdateAfterBindSampledImages,
                          IndexingProperties.maxPerStageDescriptorUpdateAfterBindSampledImages,
                          stageResources > storageCapacity_ ? stageResources - storageCapacity_ : 0u});
    if (imageCapacity_ == 0 || storageCapacity_ == 0) {
        logError("TextureManager: the device allows no update-after-bind sampled or storage images");
        return false;
    }
    logMessage("TextureManager: " + std::to_string(imageCapacity_) + " texture slots, " +
               std::to_string(storageCapacity_) + " storage slots");

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
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
    bindings[SAMPLED_BINDING].binding         = SAMPLED_BINDING;
    bindings[SAMPLED_BINDING].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    bindings[SAMPLED_BINDING].descriptorCount = imageCapacity_;
    bindings[SAMPLED_BINDING].stageFlags      = vk::ShaderStageFlagBits::eFragment |
                                                vk::ShaderStageFlagBits::eMeshEXT |
                                                vk::ShaderStageFlagBits::eCompute;
    bindings[STORAGE_BINDING].binding         = STORAGE_BINDING;
    bindings[STORAGE_BINDING].descriptorType  = vk::DescriptorType::eStorageImage;
    bindings[STORAGE_BINDING].descriptorCount = storageCapacity_;
    bindings[STORAGE_BINDING].stageFlags      = vk::ShaderStageFlagBits::eCompute;

    /* Partially bound: the arrays are declared at full capacity and only the
     * slots handed out are written. */
    const vk::DescriptorBindingFlags flags =
        vk::DescriptorBindingFlagBits::ePartiallyBound |
        vk::DescriptorBindingFlagBits::eUpdateAfterBind;
    const std::array<vk::DescriptorBindingFlags, 2> bindingFlags{flags, flags};
    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.bindingCount  = static_cast<uint32_t>(bindingFlags.size());
    flagsInfo.pBindingFlags = bindingFlags.data();

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings    = bindings.data();
    layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
    layoutInfo.pNext = &flagsInfo;
    if (device_.createDescriptorSetLayout(&layoutInfo, nullptr, &layout_) !=
        vk::Result::eSuccess) {
        logError("TextureManager: vkCreateDescriptorSetLayout failed");
        destroy();
        return false;
    }

    const std::array<vk::DescriptorPoolSize, 2> poolSizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, imageCapacity_},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, storageCapacity_}};
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes    = poolSizes.data();
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

TextureSlot TextureManager::reserveSlot(bool borrowed) {
    if (borrowed_.size() >= imageCapacity_) {
        logError("TextureManager: the bindless array holds " +
                 std::to_string(imageCapacity_) + " textures and is full");
        return {};
    }
    borrowed_.push_back(borrowed);
    return TextureSlot{static_cast<uint32_t>(borrowed_.size() - 1)};
}

void TextureManager::writeSampledDescriptor(TextureSlot slot, ImageRef view, vk::ImageLayout layout) {
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler     = sampler_;
    imageInfo.imageView   = view.get();
    imageInfo.imageLayout = layout;

    vk::WriteDescriptorSet write{};
    write.dstSet          = set_;
    write.dstBinding      = SAMPLED_BINDING;
    write.dstArrayElement = slot.value;
    write.descriptorCount = 1;
    write.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo      = &imageInfo;
    device_.updateDescriptorSets(1, &write, 0, nullptr);
}

void TextureManager::writeStorageDescriptor(StorageSlot slot, ImageRef view) {
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.imageView   = view.get();
    imageInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSet write{};
    write.dstSet          = set_;
    write.dstBinding      = STORAGE_BINDING;
    write.dstArrayElement = slot.value;
    write.descriptorCount = 1;
    write.descriptorType  = vk::DescriptorType::eStorageImage;
    write.pImageInfo      = &imageInfo;
    device_.updateDescriptorSets(1, &write, 0, nullptr);
}

StorageSlot TextureManager::addStorage(ImageRef view) {
    if (!device_) {
        logError("TextureManager::addStorage: called before init");
        return {};
    }
    if (!view) {
        logError("TextureManager::addStorage: empty view");
        return {};
    }
    if (storageCount_ >= storageCapacity_) {
        logError("TextureManager::addStorage: the storage array holds " +
                 std::to_string(storageCapacity_) + " images and is full");
        return {};
    }
    const StorageSlot slot{storageCount_++};
    writeStorageDescriptor(slot, view);
    return slot;
}

bool TextureManager::replaceStorage(StorageSlot slot, ImageRef view) {
    if (!slot || slot.value >= storageCount_) {
        logError("TextureManager::replaceStorage: slot was not handed out by addStorage");
        return false;
    }
    if (!view) {
        logError("TextureManager::replaceStorage: empty view");
        return false;
    }
    writeStorageDescriptor(slot, view);
    return true;
}

TextureSlot TextureManager::addSampledBorrowed(ImageRef view, vk::ImageLayout layout) {
    if (!device_) {
        logError("TextureManager::addBorrowed: called before init");
        return {};
    }
    if (!view) {
        logError("TextureManager::addBorrowed: empty view");
        return {};
    }
    const TextureSlot slot = reserveSlot(true);
    if (slot) writeSampledDescriptor(slot, view, layout);
    return slot;
}

bool TextureManager::replaceSampledBorrowed(TextureSlot slot, ImageRef view, vk::ImageLayout layout) {
    if (!slot || slot.value >= borrowed_.size() || !borrowed_[slot.value]) {
        logError("TextureManager::replaceBorrowed: slot was not handed out by addBorrowed");
        return false;
    }
    if (!view) {
        logError("TextureManager::replaceBorrowed: empty view");
        return false;
    }
    writeSampledDescriptor(slot, view, layout);
    return true;
}

TextureSlot TextureManager::add(BufferManager& buffers, vk::CommandBuffer commandBuffer,
                                std::span<const std::byte> levels, vk::Format format,
                                vk::Extent2D extent, uint32_t levelCount) {
    if (!device_ || !commandBuffer) {
        logError("TextureManager::add: called before init");
        return {};
    }
    if (levels.empty() || extent.width == 0 || extent.height == 0 || levelCount == 0) {
        logError("TextureManager::add: empty texture");
        return {};
    }
    const AssetFormat assetFormat{static_cast<uint32_t>(format)};
    if (assetLevelBytes(assetFormat, 1, 1) == 0) {
        logError("TextureManager::add: format " + vk::to_string(format) +
                 " has no known level size");
        return {};
    }
    if (borrowed_.size() >= imageCapacity_) {
        logError("TextureManager::add: the bindless array holds " +
                 std::to_string(imageCapacity_) + " textures and is full");
        return {};
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
            return {};
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
        return {};
    }
    std::memcpy(upload.host, levels.data(), levels.size());
    for (vk::BufferImageCopy2& region : regions)
        region.bufferOffset += upload.region.offset;

    Image& image = images_.emplace_back();
    if (!image.allocate(allocator_,
                        ImageDesc{.format    = format,
                                  .extent    = vk::Extent3D{extent.width, extent.height, 1},
                                  .usage     = vk::ImageUsageFlagBits::eSampled |
                                               vk::ImageUsageFlagBits::eTransferDst,
                                  .mipLevels = levelCount})) {
        images_.pop_back();
        return {};
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
        return {};
    }

    const TextureSlot slot = reserveSlot(false);
    if (!slot) {
        images_.pop_back();
        return {};
    }
    writeSampledDescriptor(slot, view, vk::ImageLayout::eShaderReadOnlyOptimal);
    return slot;
}

void TextureManager::destroy() {
    if (!device_) return;

    for (Image& image : images_) image.destroy();
    images_.clear();
    borrowed_.clear();
    storageCount_ = 0;

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
