module;

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

module TextureManager;

import vulkan;
import vk_mem_alloc;
import Logger;
import VkUtil;

namespace {

constexpr vk::DeviceSize TEXTURE_UPLOAD_ALIGNMENT = 16;

/// Levels down to 1x1.
uint32_t mipLevelCount(vk::Extent2D extent) {
    const uint32_t largest = std::max(extent.width, extent.height);
    return largest == 0 ? 1u : std::bit_width(largest);
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

    /* Partially bound: a material with no texture leaves its slot unwritten. */
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
                             std::span<const std::byte> pixels, vk::Format format,
                             vk::Extent2D extent) {
    if (!device_ || !commandBuffer) {
        logError("TextureManager::add: called before init");
        return INVALID_TEXTURE_INDEX;
    }
    if (pixels.empty() || extent.width == 0 || extent.height == 0) {
        logError("TextureManager::add: empty texture");
        return INVALID_TEXTURE_INDEX;
    }
    if (images_.size() >= capacity_) {
        logError("TextureManager::add: the bindless array holds " +
                 std::to_string(capacity_) + " textures and is full");
        return INVALID_TEXTURE_INDEX;
    }

    const MappedSpan<std::byte> upload =
        buffers.allocateUpload(pixels.size(), TEXTURE_UPLOAD_ALIGNMENT);
    if (!upload) {
        logError("TextureManager::add: no upload space for " +
                 std::to_string(pixels.size()) + " bytes");
        return INVALID_TEXTURE_INDEX;
    }
    std::memcpy(upload.host, pixels.data(), pixels.size());

    const uint32_t     mipLevels = mipLevelCount(extent);
    const vk::Extent3D extent3D{extent.width, extent.height, 1};

    Image& image = images_.emplace_back();
    if (!image.allocate(allocator_, vk::ImageType::e2D, format, extent3D,
                        vk::ImageUsageFlagBits::eSampled |
                            vk::ImageUsageFlagBits::eTransferDst |
                            vk::ImageUsageFlagBits::eTransferSrc,
                        mipLevels)) {
        images_.pop_back();
        return INVALID_TEXTURE_INDEX;
    }

    transitionImage(commandBuffer, image.handle(), vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::PipelineStageFlagBits2::eTopOfPipe, {},
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::ImageAspectFlagBits::eColor, 0, mipLevels);

    vk::BufferImageCopy2 region{};
    region.bufferOffset     = upload.region.offset;
    region.imageSubresource = colorLayer(0);
    region.imageExtent      = extent3D;

    vk::CopyBufferToImageInfo2 copyInfo{};
    copyInfo.srcBuffer      = upload.region.buffer;
    copyInfo.dstImage       = image.handle();
    copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
    copyInfo.regionCount    = 1;
    copyInfo.pRegions       = &region;
    commandBuffer.copyBufferToImage2(copyInfo);

    /* Each level is blitted from the one above it, so that level must finish
     * being written before it is read. */
    vk::Extent2D source = extent;
    for (uint32_t level = 1; level < mipLevels; ++level) {
        transitionImage(commandBuffer, image.handle(),
                        vk::ImageLayout::eTransferDstOptimal,
                        vk::ImageLayout::eTransferSrcOptimal,
                        vk::PipelineStageFlagBits2::eTransfer,
                        vk::AccessFlagBits2::eTransferWrite,
                        vk::PipelineStageFlagBits2::eTransfer,
                        vk::AccessFlagBits2::eTransferRead,
                        vk::ImageAspectFlagBits::eColor, level - 1, 1);

        const vk::Extent2D destination{std::max(source.width / 2, 1u),
                                       std::max(source.height / 2, 1u)};

        vk::ImageBlit2 blit{};
        blit.srcSubresource = colorLayer(level - 1);
        blit.srcOffsets[1]  = vk::Offset3D{static_cast<int32_t>(source.width),
                                           static_cast<int32_t>(source.height), 1};
        blit.dstSubresource = colorLayer(level);
        blit.dstOffsets[1]  = vk::Offset3D{static_cast<int32_t>(destination.width),
                                           static_cast<int32_t>(destination.height), 1};

        vk::BlitImageInfo2 blitInfo{};
        blitInfo.srcImage       = image.handle();
        blitInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        blitInfo.dstImage       = image.handle();
        blitInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        blitInfo.regionCount    = 1;
        blitInfo.pRegions       = &blit;
        blitInfo.filter         = vk::Filter::eLinear;
        commandBuffer.blitImage2(blitInfo);

        source = destination;
    }

    if (mipLevels > 1)
        transitionImage(commandBuffer, image.handle(),
                        vk::ImageLayout::eTransferSrcOptimal,
                        vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::PipelineStageFlagBits2::eTransfer,
                        vk::AccessFlagBits2::eTransferRead,
                        vk::PipelineStageFlagBits2::eFragmentShader,
                        vk::AccessFlagBits2::eShaderSampledRead,
                        vk::ImageAspectFlagBits::eColor, 0, mipLevels - 1);

    transitionImage(commandBuffer, image.handle(),
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eFragmentShader,
                    vk::AccessFlagBits2::eShaderSampledRead,
                    vk::ImageAspectFlagBits::eColor, mipLevels - 1, 1);

    const ImageRef view = image.view(
        vk::ImageViewType::e2D, format,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1});
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
