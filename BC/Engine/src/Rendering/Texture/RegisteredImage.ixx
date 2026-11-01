module;

#include <cstdint>

export module RegisteredImage;

import vulkan;
import vk_mem_alloc;
import Image;
import TextureManager;

/**
 * How image should be stored in the texture manager
 * imageAspect: aspect of the sampled slot's view of every mip; empty for no sampled slot.
 * imageLayout: layout the image is in whenever a shader samples it.
 * storageMips: one storage slot per mip level, in level order.
 */
export struct RegisteredImageParameters {
    vk::ImageAspectFlags imageAspect{};
    vk::ImageLayout      imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    bool                 storageMips = false;

    bool operator==(const RegisteredImageParameters&) const = default;
};

export struct RegisteredImageAllocationParameters {
    vma::Allocator allocator;
    TextureManager& textureManager;
    const ImageDesc& desc;
    const RegisteredImageParameters& registration;
};

/**
 * Owns an Image and the TextureManager slots its views are written into.
 * Slots may be retired when GPU is done with them, so always ask for them.
 */
export class RegisteredImage {
public:
    RegisteredImage() = default;
    ~RegisteredImage() { destroy(); }

    RegisteredImage(const RegisteredImage&)            = delete;
    RegisteredImage& operator=(const RegisteredImage&) = delete;

    /**
     * Allocates the image and registers it with texture manager.
     * @return false after logging when the allocation or a slot write failed.
     * @note textures must outlive this object or RegisteredImage::destroy() must be called first.
     * @note desc.usage must hold eSampled for a sampled slot and eStorage for storage mips.
     */
    [[nodiscard]] bool allocate(RegisteredImageAllocationParameters allocationParameters);

    /**
     * Frees the image, allocates a new one and writes its views into the slots.
     * @param desc parameters of the new image.
     * @return false after logging when the allocation or a slot write failed.
     * @note The GPU must be done with the image and its slots before this is called.
     */
    [[nodiscard]] bool reallocate(const ImageDesc& desc);

    /**
     * Retires the slots and destroys the image. Safe to call twice.
     * @note The GPU must be done with the image before this is called.
     */
    void destroy();

    [[nodiscard]] Image&       image()       { return image_; }
    [[nodiscard]] const Image& image() const { return image_; }

    /// @return slot of the view covering every mip level; empty without a sampled registration.
    [[nodiscard]] TextureSlot sampledSlot() const { return sampledSlot_; }

    /// @return one storage slot per mip level; empty without storage mips.
    [[nodiscard]] StorageRun storageMips() const { return storageMips_; }

    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(image_); }

private:
    /// Writes the image's views into the slots, taking the ones not yet taken.
    [[nodiscard]] bool writeSlots();

    Image             image_;
    TextureManager*   texturesManager_ = nullptr;
    RegisteredImageParameters registrationParams_{};
    TextureSlot       sampledSlot_;
    StorageRun        storageMips_;
};
