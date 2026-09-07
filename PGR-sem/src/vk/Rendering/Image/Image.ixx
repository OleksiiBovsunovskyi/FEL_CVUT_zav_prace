module;

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

export module Image;

import Logger;

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
export class Image {
public:
    Image() = default;
    ~Image() { destroy(); }

    Image(const Image&)            = delete;
    Image& operator=(const Image&) = delete;

    /**
     * Allocates the image and its memory, replacing whatever this held before.
     *
     * @param allocator VMA allocator the image is allocated from;
     * @param imageType dimensions of the image.
     * @param format texel format.
     * @param extent size in texels.
     * @param usage every way the image will ever be used.
     * @param mipLevels levels in the chain.
     * @param arrayLayers layers.
     * @param samples samples per texel.
     * @return false when the allocation failed.
     */
    [[nodiscard]] bool allocate(vma::Allocator allocator, vk::ImageType imageType,
                                vk::Format format, vk::Extent3D extent,
                                vk::ImageUsageFlags usage,
                                uint32_t mipLevels = 1, uint32_t arrayLayers = 1,
                                vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1) {
        assert(allocator && "Image::allocate: null vma::Allocator");
        destroy();

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType     = imageType;
        imageInfo.format        = format;
        imageInfo.extent        = extent;
        imageInfo.mipLevels     = mipLevels;
        imageInfo.arrayLayers   = arrayLayers;
        imageInfo.samples       = samples;
        imageInfo.tiling        = vk::ImageTiling::eOptimal;
        imageInfo.usage         = usage;
        imageInfo.sharingMode   = vk::SharingMode::eExclusive;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;

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
        format_     = format;
        extent_     = extent;
        return true;
    }

    /**
     * Takes an image somebody else allocated, replacing whatever this held before.
     * destroy() then frees only the views taken here.
     *
     * @param device device the views are created on.
     * @param image image to borrow
     * @param format texel format the image was created with.
     * @param extent size in texels the image was created with.
     * @note Borrowed image will not be owned by this class.
     * @note View ranges are not validated for a borrowed image; its mip and layer
     *       counts are not known here.
     * @note An ImageRef taken from a borrowed image is not invalidated when the
     *       owner destroys or recreates that image.
     */
    void borrow(vk::Device device, vk::Image image, vk::Format format, vk::Extent3D extent) {
        assert(device && "Image::borrow: null vk::Device");
        destroy();

        device_ = device;
        image_  = image;
        format_ = format;
        extent_ = extent;
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

    /// @return version used to invalidate ImageRef.
    [[nodiscard]] uint32_t version() const { return version_; }

    [[nodiscard]] vk::Image    handle() const { return image_; }
    [[nodiscard]] vk::Format   format() const { return format_; }
    [[nodiscard]] vk::Extent3D extent() const { return extent_; }
    [[nodiscard]] vk::Device   device() const { return device_; }

    /// @return true if image is owned by this class, false if borrowed.
    [[nodiscard]] bool owned() const { return allocation_ != nullptr; }

    /// @return true while an image is valid, (owned or borrowed).
    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(image_); }

private:
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

    vk::Format   format_ = vk::Format::eUndefined;
    vk::Extent3D extent_{};

    uint32_t version_ = 0;
};

vk::ImageView ImageRef::get() const {
    assert(image_ && version_ == image_->version() &&
           "ImageRef: the image was recreated after this view was taken");
    return view_;
}
