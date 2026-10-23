module;

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

export module Image;

import vulkan;
import vk_mem_alloc;
import Logger;
import Resource;

/* Warns at every call site. Clang only;*/
#if defined(__clang__)
#  define IMAGE_EXPENSIVE(msg) __attribute__((diagnose_if(true, msg, "warning")))
#else
#  define IMAGE_EXPENSIVE(msg)
#endif

/**
 * Parameters a vk::Image is created with.
 * usage is empty for a borrowed image.
 */
export struct ImageDesc {
    vk::ImageType           type    = vk::ImageType::e2D;
    vk::Format              format  = vk::Format::eUndefined;
    vk::Extent3D            extent{1, 1, 1};
    vk::ImageUsageFlags     usage{};
    uint32_t                mipLevels   = 1;
    uint32_t                arrayLayers = 1;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;

    bool operator==(const ImageDesc&) const = default;
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

export class Image;
/**
 * An image view.
 * Holds and asserts the version of the image it was taken from (debug builds only).
 */
export class ImageRef {
public:
    ImageRef() = default;

    /*
     * @return the view. 
     * Debug builds assert the image has not been recreated since.
     **/
    [[nodiscard]] vk::ImageView get() const;

    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(view_); }

private:
    friend class Image;

    ImageRef(const Image* image, vk::ImageView view, uint32_t version)
        : image_(image), view_(view), version_(version) {}

    const Image*  image_   = nullptr;
    vk::ImageView view_    = nullptr;
    uint32_t      version_ = 0;
};

/**
 *Owns or borrows the vk::Image, owns its views
 *Owned vk::Image to be allocated with Image::allocate().
 *Borrowed image to be set with Image::borrow().
 *Owned images and views are destroyed on destruction
 */
export class Image : public IResource {
public:
    Image() = default;
    //We override default IResource destructor.
    ~Image() override {  destroy(); }

    Image(const Image&)            = delete;
    Image& operator=(const Image&) = delete;
    /**
    * Aspect defined by the given image format.
    * @param format format of the image
    * @return aspect from given format.
    */
    static constexpr vk::ImageAspectFlags aspectOf(vk::Format format) {
        vk::ImageAspectFlags aspect{};
        if (vk::hasDepthComponent(format))   aspect |= vk::ImageAspectFlagBits::eDepth;
        if (vk::hasStencilComponent(format)) aspect |= vk::ImageAspectFlagBits::eStencil;
        return aspect ? aspect : vk::ImageAspectFlagBits::eColor;
    }
    
    /**
     * Allocates the image and its memory, replacing whatever this held before.
     *
     * @param allocator VMA allocator the image is allocated from;
     * @param desc parameters of the image.
     * @return false when the allocation failed.
     */
    [[nodiscard]] bool allocate(vma::Allocator allocator, const ImageDesc& desc) {
        assert(allocator && "Image::allocate: null vma::Allocator");
        assert(desc.usage != vk::ImageUsageFlags{} && "Image::allocate: usage names no use");
        validate(desc);
        destroy();

        vk::ImageCreateInfo imageInfo{};
        {
            imageInfo.imageType     = desc.type;
            imageInfo.format        = desc.format;
            imageInfo.extent        = desc.extent;
            imageInfo.mipLevels     = desc.mipLevels;
            imageInfo.arrayLayers   = desc.arrayLayers;
            imageInfo.samples       = desc.samples;
            imageInfo.tiling        = vk::ImageTiling::eOptimal;
            imageInfo.usage         = desc.usage;
            imageInfo.sharingMode   = vk::SharingMode::eExclusive;
            imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        } //Fill out image create info from desc
        vma::AllocationCreateInfo allocInfo{};
        allocInfo.usage = vma::MemoryUsage::eAutoPreferDevice;

        vk::Image       image      = nullptr;
        vma::Allocation allocation = nullptr;
        const vk::Result result =
            allocator.createImage(&imageInfo, &allocInfo, &image, &allocation, nullptr);
        if (result != vk::Result::eSuccess) {
            logError("Image::allocate: createImage failed: " + vk::to_string(result));
            return false;
        }

        allocator_  = allocator;
        device_     = allocator.getAllocatorInfo().device;
        image_      = image;
        allocation_ = allocation;
        desc_       = desc;
        return true;
    }

    /**
     * Frees the owned image and allocates a new one from the same allocator.
     * Does nothing when desc equals the current parameters.
     *
     * @param desc parameters of the new image.
     * @return false when the image is borrowed or the allocation failed.
     * @note Every view of the old image is invalidated.
     * @note The GPU must be done with the image before this is called.
     */
    [[nodiscard]] bool reallocate(const ImageDesc& desc)
        IMAGE_EXPENSIVE("Image::reallocate frees and reallocates the image; every "
                        "view of it is invalidated and the GPU must be done with it")
    {
        assert(owned() && "Image::reallocate: cannot reallocate a borrowed image");
        if (!owned()) {
            logError("Image::reallocate: cannot reallocate a borrowed image");
            return false;
        }
        if (desc == desc_) return true;
        const vma::Allocator allocator = allocator_;
        return allocate(allocator, desc);
    }

    /**
     * Takes an image somebody else allocated, replacing whatever this held before.
     * destroy() then frees only the views taken here.
     *
     * @param device device the views are created on.
     * @param image image to borrow
     * @param desc parameters the image was created with.
     * @note Borrowed image will not be owned by this class.
     * @note An ImageRef taken from a borrowed image is not invalidated when the
     *       owner destroys or recreates that image.
     */
    void borrow(vk::Device device, vk::Image image, const ImageDesc& desc) {
        assert(device && "Image::borrow: null vk::Device");
        validate(desc);
        destroy();

        allocator_ = nullptr;
        device_    = device;
        image_     = image;
        desc_      = desc;
    }

    /**
     * Destroys every view, and the image itself when allocate() created it.
     * Bumps the version, for ImageRef validation.
     */
    void destroy() {
        assert((views_.empty() || device_) && "Image::destroy: views without a device");
        for (const CachedView& cached : views_) device_.destroyImageView(cached.view);
        views_.clear();

        if (image_ && allocation_) allocator_.destroyImage(image_, allocation_);
        image_      = nullptr;
        allocation_ = nullptr;
        ++version_;
    }

    /**
     * @param viewType how the view reads the image's dimensions and layers.
     * @param format texel format the view reinterprets the image as.
     * @param range mip levels and array layers the view covers.
     * @return a view with those parameters, created if necessary, cached otherwise, or empty if when creation failed.
     */
    [[nodiscard]] ImageRef view(vk::ImageViewType viewType, vk::Format format,
                                const vk::ImageSubresourceRange& range) {
        assert(image_ && "Image::view: no image to take a view of");

        for (const CachedView& cached : views_)
            if (cached.viewType == viewType && cached.format == format &&
                cached.range == range)
                return ImageRef{this, cached.view, version_};

        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image            = image_;
        viewInfo.viewType         = viewType;
        viewInfo.format           = format;
        viewInfo.subresourceRange = range;

        vk::ImageView view = nullptr;
        const vk::Result result = device_.createImageView(&viewInfo, nullptr, &view);
        if (result != vk::Result::eSuccess) {
            logError("Image::view: createImageView failed: " + vk::to_string(result));
            return {};
        }

        views_.push_back(CachedView{viewType, format, range, view});
        return ImageRef{this, view, version_};
    }

    /**
     * @param aspect color, depth or stencil.
     * @return a view of every mip level and array layer in the image's format. Empty when creation failed.
     */
    [[nodiscard]] ImageRef view(vk::ImageAspectFlags aspect) {
        return view(viewTypeFor(desc_.type, desc_.arrayLayers), desc_.format,
                    vk::ImageSubresourceRange{aspect, 0, desc_.mipLevels, 0, desc_.arrayLayers});
    }

    /// @return version used to invalidate ImageRef.
    [[nodiscard]] uint32_t version() const { return version_; }

    [[nodiscard]] vk::Image        handle()      const { return image_; }
    [[nodiscard]] const ImageDesc& desc()        const { return desc_; }
    [[nodiscard]] vk::Format       format()      const { return desc_.format; }
    [[nodiscard]] vk::Extent3D     extent()      const { return desc_.extent; }
    [[nodiscard]] uint32_t         mipLevels()   const { return desc_.mipLevels; }
    [[nodiscard]] uint32_t         arrayLayers() const { return desc_.arrayLayers; }
    [[nodiscard]] vk::Device       device()      const { return device_; }

    /// @return true if image is owned by this class, false if borrowed.
    [[nodiscard]] bool owned() const { return allocation_ != nullptr; }

    /// @return true while an image is valid, (owned or borrowed).
    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(image_); }

protected:
    
    /// IResource interface
    void collect(const ResourceAccess& targetAccess, BarrierList& list) override
    {
        assert(image_ && "Image::collect: no image to barrier");
        vk::ImageMemoryBarrier2 barrier{};
        const std::optional<ResourceAccess> previous = exchangeAccess(targetAccess);
        if (!previous) return; //No barrier needed
        barrier.image = image_;
        barrier.srcStageMask = previous->stageMask;
        barrier.dstStageMask = targetAccess.stageMask;
        barrier.srcAccessMask = previous->accessMask;
        barrier.dstAccessMask = targetAccess.accessMask;
        barrier.oldLayout = previous->layout;
        barrier.newLayout = targetAccess.layout;
        barrier.subresourceRange = {aspectOf(desc_.format), 0, desc_.mipLevels, 0, desc_.arrayLayers};
        list.add(barrier);
    }
    
private:
    /**
     * Debug builds only.
     * Validates image parameters with asserts.
     */
    static void validate(const ImageDesc& desc) {
        assert(desc.format != vk::Format::eUndefined && "Image: format is UNDEFINED");
        assert(extentFitsType(desc.type, desc.extent) &&
               "Image: extent members must be at least 1, and 1 in every "
               "dimension the image type does not have");
        assert(desc.mipLevels >= 1 && desc.mipLevels <= maxMipLevels(desc.extent) &&
               "Image: mipLevels past what the extent can halve to 1");
        assert(desc.arrayLayers >= 1 && "Image: arrayLayers must be at least 1");
        assert((desc.type != vk::ImageType::e3D || desc.arrayLayers == 1) &&
               "Image: a 3D image has one array layer");
        assert(std::has_single_bit(static_cast<uint32_t>(desc.samples)) &&
               "Image: samples must be one vk::SampleCountFlagBits value");
        (void)desc;
    }

    struct CachedView {
        vk::ImageViewType         viewType;
        vk::Format                format;
        vk::ImageSubresourceRange range;
        vk::ImageView             view;
    };

    std::vector<CachedView> views_;

    vma::Allocator  allocator_  = nullptr;
    vma::Allocation allocation_ = nullptr;
    vk::Device      device_     = nullptr;
    vk::Image       image_      = nullptr;

    ImageDesc desc_{};

    uint32_t version_ = 0;


};

vk::ImageView ImageRef::get() const {
    assert(image_ && version_ == image_->version() &&
           "ImageRef: the image was recreated after this view was taken");
    return view_;
}
