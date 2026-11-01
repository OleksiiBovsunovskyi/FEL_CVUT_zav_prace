module;

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

module RegisteredImage;

import vulkan;
import vk_mem_alloc;
import Logger;
import Image;
import TextureManager;

bool RegisteredImage::allocate(RegisteredImageAllocationParameters allocationParameters) {
    assert((!texturesManager_ || (texturesManager_ == &allocationParameters.textureManager && registrationParams_ == allocationParameters.registration)) &&
           "RegisteredImage::allocate: image manager is invalid or image was registered with different registrarion params");
    if (!image_.allocate(allocationParameters.allocator, allocationParameters.desc)) {
        logError("RegisteredImage::allocate: image allocation failed");
        return false;
    }
    texturesManager_     = &allocationParameters.textureManager;
    registrationParams_ = allocationParameters.registration;
    return writeSlots();
}

bool RegisteredImage::reallocate(const ImageDesc& desc) {
    assert(texturesManager_ && "RegisteredImage::reallocate: called before allocate");
    if (!image_.reallocate(desc)) return false;
    return writeSlots();
}

void RegisteredImage::destroy() {
    if (texturesManager_) {
        if (sampledSlot_) texturesManager_->retireSampledBorrowed(sampledSlot_);
        if (storageMips_) texturesManager_->retireStorage(storageMips_);
    }
    sampledSlot_        = {};
    storageMips_        = {};
    texturesManager_    = nullptr;
    registrationParams_ = {};
    image_.destroy();
}

bool RegisteredImage::writeSlots() {
    const ImageDesc& desc = image_.desc();
    ///register the sampled view of image
    if (registrationParams_.imageAspect) {
        if (!(desc.usage & vk::ImageUsageFlagBits::eSampled)) {
            logError("RegisteredImage: a sampled slot needs an image with eSampled usage");
            return false;
        }
        const ImageRef view = image_.view(registrationParams_.imageAspect);
        if (sampledSlot_) {
            if (!texturesManager_->replaceSampledBorrowed(sampledSlot_, view, registrationParams_.imageLayout))
                return false;
        } else {
            sampledSlot_ = texturesManager_->addSampledBorrowed(view, registrationParams_.imageLayout);
            if (!sampledSlot_) return false;
        }
    }
    ///Register image mips
    if (!registrationParams_.storageMips) return true;
    if (!(desc.usage & vk::ImageUsageFlagBits::eStorage)) {
        logError("RegisteredImage: storage mips need an image with eStorage usage");
        return false;
    }
    const vk::ImageAspectFlags aspect = Image::aspectOf(desc.format);
    std::vector<ImageRef> views;
    views.reserve(desc.mipLevels);
    for (uint32_t level = 0; level < desc.mipLevels; ++level)
        views.push_back(image_.mipView(aspect, level));

    if (storageMips_.count == desc.mipLevels) {
        for (uint32_t level = 0; level < desc.mipLevels; ++level)
            if (!texturesManager_->replaceStorage(storageMips_[level], views[level])) return false;
        return true;
    }
    if (storageMips_) texturesManager_->retireStorage(storageMips_);
    storageMips_ = texturesManager_->addStorage(views);
    return static_cast<bool>(storageMips_);
}
