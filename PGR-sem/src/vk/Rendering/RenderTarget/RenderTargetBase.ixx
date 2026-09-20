module;

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <string>

export module renderTarget:RenderTarget;

import vulkan;
import vk_mem_alloc;
import Image;
import Logger;

/* Warns at every call site. Clang only;*/
#if defined(__clang__)
#  define RT_EXPENSIVE(msg) __attribute__((diagnose_if(true, msg, "warning")))
#else
#  define RT_EXPENSIVE(msg)
#endif

//Helpers used for validation and tests 
export constexpr uint32_t maxMipLevels(vk::Extent3D extent);
export constexpr bool extentFitsType(vk::ImageType imageType, vk::Extent3D extent);

//helper used for test, validation and RenderTarget internally
export constexpr vk::ImageViewType viewTypeFor(vk::ImageType imageType, uint32_t arrayLayers);

/**
 * Base class for any render target.
 * Actual image parameters are to be decided by the child class.
 */
export class RenderTarget {
public:
    /**
     * Names a render target. The image is allocated with create().
     *
     * @param allocator VMA allocator the image is allocated from.
     * @param imageType dimensions of the image
     * @param format texel format.
     * @param extent size in texels; every member must be at least 1.
     * @param usage every way the image will ever be used.
     * @param mipLevels levels in the chain.
     * @param arrayLayers layers;
     * @param samples samples per texel; every attachment in one pass must agree.
     */
    RenderTarget(vma::Allocator allocator, vk::ImageType imageType, vk::Format format,
                 vk::Extent3D extent, vk::ImageUsageFlags usage,
                 uint32_t mipLevels = 1, uint32_t arrayLayers = 1,
                 vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1)
        : allocator_(allocator), imageType_(imageType), format_(format),
          extent_(extent), usage_(usage), mipLevels_(mipLevels),
          arrayLayers_(arrayLayers), samples_(samples)
    {
      validate();
    }

    /**
     * Constructs a render target borrowing an existing vk::Image.
     *
     * @param device device the views are created on.
     * @param image image to borrow.
     * @param imageType dimensions of the image.
     * @param format texel format.
     * @param extent size in texels.
     * @param mipLevels levels in the chain.
     * @param arrayLayers layers.
     * @param samples samples per texel.
     * @note Borrowed image will not be owned by this class.
     */
    RenderTarget(vk::Device device, vk::Image image, vk::ImageType imageType,
                 vk::Format format, vk::Extent3D extent,
                 uint32_t mipLevels = 1, uint32_t arrayLayers = 1,
                 vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1)
        : imageType_(imageType), format_(format), extent_(extent),
          mipLevels_(mipLevels), arrayLayers_(arrayLayers), samples_(samples)
    {
        image_.borrow(device, image, format, extent);
        validate();
    }

    virtual ~RenderTarget() = default;

    RenderTarget(const RenderTarget&)            = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    /**
     * Borrows an existing vk::Image, replacing whatever was held.
     *
     * @param device device the views are created on.
     * @param image image to borrow.
     * @param imageType dimensions of the image.
     * @param format texel format.
     * @param extent size in texels.
     * @param mipLevels levels in the chain.
     * @param arrayLayers layers.
     * @param samples samples per texel.
     * @note Borrowed image will not be owned by this class.
     */
    void borrow(vk::Device device, vk::Image image, vk::ImageType imageType,
                vk::Format format, vk::Extent3D extent,
                uint32_t mipLevels = 1, uint32_t arrayLayers = 1,
                vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1) {
        destroy();
        allocator_   = nullptr;
        imageType_   = imageType;
        format_      = format;
        extent_      = extent;
        usage_       = {};
        mipLevels_   = mipLevels;
        arrayLayers_ = arrayLayers;
        samples_     = samples;
        image_.borrow(device, image, format, extent);
        validate();
    }

    /**
     * Allocates the image. Call on already allocated render target does nothing.
     * Fails if image is borrowed.
     * @return false when the allocation failed;
     * */
    [[nodiscard]] bool create() {
        if (image_) return true;
        assert(owned() && "RenderTarget::create: cannot allocate a borrowed image");
        if (!owned()) {
            logError("RenderTarget::create: cannot allocate a borrowed image");
            return false;
        }
        validate();
        return image_.allocate(allocator_, imageType_, format_, extent_, usage_,
                               mipLevels_, arrayLayers_, samples_);
    }

    /**
     * Resizes the render target, recreating the image and invalidating every view of it.
     * Fails on borrowed image
     * @note The GPU must be done with the image before this is called.
     */
    [[nodiscard]] virtual bool resize(vk::Extent3D extent)
        RT_EXPENSIVE("RenderTarget::resize frees and reallocates the image; every "
                     "view of it is invalidated and the GPU must be done with it")
    {
        assert(owned() && "RenderTarget::resize: cannot reallocate a borrowed image");
        if (!owned()) {
            logError("RenderTarget::resize: cannot reallocate a borrowed image");
            return false;
        }
        //We may not need to resize this render target
        if (image_ && extent_ == extent) return true;

        validate();
        assert(extentFitsType(imageType_, extent) &&
               "RenderTarget::resize: extent does not fit the image type");
        destroy();
        extent_ = extent;
        if (!create()) {
            logError("RenderTarget::resize: image creation failed, image is now invalid");
            return false;
        }
        return true;
    }

    /**
     * Destroys every view and clears the image.
     * Image memory allocated by this target is freed.
     */
    void destroy() {
        image_.destroy();
    }

    /**
     * @param aspect color, depth or stencil.
     * @return a view of every mip level and array layer. Empty when creation failed.
     */
    [[nodiscard]] ImageRef view(vk::ImageAspectFlags aspect) {
        return image_.view(viewTypeFor(imageType_, arrayLayers_), format_,
                           vk::ImageSubresourceRange{aspect, 0, mipLevels_, 0, arrayLayers_});
    }

    [[nodiscard]] const Image&        image()       const { return image_; }
    [[nodiscard]] vk::Format          format()      const { return format_; }
    [[nodiscard]] vk::Extent3D        extent()      const { return extent_; }
    [[nodiscard]] vk::ImageUsageFlags usage()       const { return usage_; }
    [[nodiscard]] uint32_t            mipLevels()   const { return mipLevels_; }
    [[nodiscard]] uint32_t            arrayLayers() const { return arrayLayers_; }

    /// @return true if image is allocated and owned by this class, false if borrowed.
    [[nodiscard]] bool owned() const { return allocator_ != nullptr; }

    /// @return true if image held by this render target is valid. (After successful create() and before destroy().)
    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(image_); }

protected:
    /// Views of one mip level or one array layer are taken through this.
    [[nodiscard]] Image& mutableImage() { return image_; }

private:
    /**
     * Debug builds only.
     * Validates render target parameters with asserts.
     */
    void validate() const
    {
        assert((!owned() || allocator_) && "RenderTarget: null vma::Allocator");
        assert(format_ != vk::Format::eUndefined && "RenderTarget: format is UNDEFINED");
        assert((!owned() || usage_ != vk::ImageUsageFlags{}) && "RenderTarget: usage names no use");
        assert(extentFitsType(imageType_, extent_) &&
               "RenderTarget: extent members must be at least 1, and 1 in every "
               "dimension the image type does not have");
        assert(mipLevels_ >= 1 && mipLevels_ <= maxMipLevels(extent_) &&
               "RenderTarget: mipLevels past what the extent can halve to 1");
        assert(arrayLayers_ >= 1 && "RenderTarget: arrayLayers must be at least 1");
        assert((imageType_ != vk::ImageType::e3D || arrayLayers_ == 1) &&
               "RenderTarget: a 3D image has one array layer");
        assert(std::has_single_bit(static_cast<uint32_t>(samples_)) &&
               "RenderTarget: samples must be one vk::SampleCountFlagBits value");
    }

    Image image_;

    vma::Allocator allocator_ = nullptr;

    vk::ImageType           imageType_;
    vk::Format              format_;
    vk::Extent3D            extent_;
    vk::ImageUsageFlags     usage_;
    uint32_t                mipLevels_;
    uint32_t                arrayLayers_;
    vk::SampleCountFlagBits samples_;
};

/// @return true when every extent member is at least 1 and is 1 in the
///         dimensions imageType does not have.
export constexpr bool extentFitsType(vk::ImageType imageType, vk::Extent3D extent) {
    if (extent.width == 0 || extent.height == 0 || extent.depth == 0) return false;
    if (imageType == vk::ImageType::e1D) return extent.height == 1 && extent.depth == 1;
    if (imageType == vk::ImageType::e2D) return extent.depth == 1;
    return true;
}

/// @return levels in a chain that halves extent down to 1 in every dimension.
export constexpr uint32_t maxMipLevels(vk::Extent3D extent) {
    return std::bit_width(std::max({extent.width, extent.height, extent.depth}));
}

/// @return the view type covering every layer of an image of this type.
export constexpr vk::ImageViewType viewTypeFor(vk::ImageType imageType, uint32_t arrayLayers) {
    if (imageType == vk::ImageType::e1D)
        return arrayLayers > 1 ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D;
    if (imageType == vk::ImageType::e2D)
        return arrayLayers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
    return vk::ImageViewType::e3D;
}
