module;

#include <cstdint>
#include <deque>
#include <span>
#include <vector>

export module TextureManager;

import vulkan;
import vk_mem_alloc;
import VulkanContext;
import BufferManager;
import Image;
export import GPUTypes;

/**
 * Index into the bindless sampled-image array.
 */
export struct TextureSlot {
    uint32_t value = INVALID_TEXTURE_INDEX;

    [[nodiscard]] explicit operator bool() const { return value != INVALID_TEXTURE_INDEX; }
};

/**
 * Index into the bindless storage-image array.
 */
export struct StorageSlot {
    uint32_t value = INVALID_TEXTURE_INDEX;

    [[nodiscard]] explicit operator bool() const { return value != INVALID_TEXTURE_INDEX; }
};

/**
 * Owns the bindless image arrays, the sampler and the descriptor set they live in.
 * Binding 0: sampled images, `Sampler2D textures[]`.
 * Binding 1: storage images in eGeneral, `RWTexture2D storageImages[]`, compute only.
 * Owns the images of slots handed out by TextureManager::add().
 * Borrows the images of slots handed out by TextureManager::addBorrowed() and
 * TextureManager::addStorage().
 * Slots are never freed; a slot index outlives whoever asked for it.
 */
export class TextureManager {
public:
    TextureManager() = default;
    ~TextureManager();

    TextureManager(const TextureManager&)            = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    /**
     * Creates the sampler, the set layout and the one set.
     * @param ctx supplies the device and the allocator.
     * @return false after logging, leaving nothing allocated.
     * @note Call before Renderer::init: a pipeline layout built from layout()
     *       before this runs gets a null handle and no descriptor set.
     */
    [[nodiscard]] bool init(VulkanContext& ctx);

    void destroy();

    /**
     * Allocates the image, records the upload of every level, and writes the
     * descriptor.
     *
     * @param buffers supplies the upload space the levels are copied through.
     * @param commandBuffer an open transfer batch.
     * @param levels every mip level, tightly packed largest first.
     * @param format texel format of every level.
     * @param extent size of level 0 in texels.
     * @param levelCount levels `levels` holds.
     * @return the slot, or an empty TextureSlot after logging.
     * @note Mip levels are not generated here; a texture arrives with the chain
     *       its baker built.
     * @note The image is left in eShaderReadOnlyOptimal once the batch runs.
     * @note The descriptor is written immediately, which the set's
     *       eUpdateAfterBind flag permits while it is bound elsewhere.
     */
    [[nodiscard]] TextureSlot add(BufferManager& buffers, vk::CommandBuffer commandBuffer,
                                  std::span<const std::byte> levels, vk::Format format,
                                  vk::Extent2D extent, uint32_t levelCount);

    /**
     * Writes a view of an image owned elsewhere into a new slot.
     * @param view view the shaders sample.
     * @param layout layout the image is in whenever a shader reads the slot.
     * @return the slot, or an empty TextureSlot after logging.
     * @note Borrowed image will not be owned by this class.
     */
    [[nodiscard]] TextureSlot addSampledBorrowed(ImageRef view, vk::ImageLayout layout);

    /**
     * Rewrites a slot from TextureManager::addBorrowed() with a new view.
     * @param slot slot returned by TextureManager::addBorrowed().
     * @param view view the shaders sample.
     * @param layout layout the image is in whenever a shader reads the slot.
     * @return false after logging when the slot is not a borrowed one.
     * @note The GPU must be done with the slot before this is called.
     */
    [[nodiscard]] bool replaceSampledBorrowed(TextureSlot slot, ImageRef view, vk::ImageLayout layout);

    /**
     * Writes a view of an image owned elsewhere into a new storage slot.
     * @param view view the shaders write.
     * @return the slot, or an empty StorageSlot after logging.
     * @note Slots from consecutive calls are consecutive.
     * @note The image must be in eGeneral whenever a shader accesses the slot.
     * @note Borrowed image will not be owned by this class.
     */
    [[nodiscard]] StorageSlot addStorage(ImageRef view);

    /**
     * Rewrites a slot from TextureManager::addStorage() with a new view.
     * @param slot slot returned by TextureManager::addStorage().
     * @param view view the shaders write.
     * @return false after logging when the slot was never handed out.
     * @note The GPU must be done with the slot before this is called.
     */
    [[nodiscard]] bool replaceStorage(StorageSlot slot, ImageRef view);

    /// @return slots the sampled array holds, the device limit read by init().
    [[nodiscard]] uint32_t capacity() const { return imageCapacity_; }

    /// @return slots the storage array holds.
    [[nodiscard]] uint32_t storageCapacity() const { return storageCapacity_; }

    [[nodiscard]] vk::DescriptorSetLayout layout() const { return layout_; }
    [[nodiscard]] vk::DescriptorSet       set()    const { return set_; }

    /// @return sampled slots handed out so far.
    [[nodiscard]] uint32_t count() const { return static_cast<uint32_t>(borrowed_.size()); }

    /// @return storage slots handed out so far.
    [[nodiscard]] uint32_t storageCount() const { return storageCount_; }

    /// Storage array slots; sized for the mip views of render targets.
    static constexpr uint32_t MAX_STORAGE_IMAGES = 1024;

private:
    /// @return the next slot, or an empty TextureSlot after logging when the array is full.
    [[nodiscard]] TextureSlot reserveSlot(bool borrowed);

    void writeSampledDescriptor(TextureSlot slot, ImageRef view, vk::ImageLayout layout);
    void writeStorageDescriptor(StorageSlot slot, ImageRef view);

    /// Stable addresses: Image is not movable.
    std::deque<Image> images_;

    /// Indexed by TextureSlot::value; true for slots from TextureManager::addBorrowed().
    std::vector<bool> borrowed_;

    vk::Device              device_  = nullptr;
    vma::Allocator          allocator_ = nullptr;
    vk::Sampler             sampler_ = nullptr;
    vk::DescriptorSetLayout layout_  = nullptr;
    vk::DescriptorPool      pool_    = nullptr;
    vk::DescriptorSet       set_     = nullptr;
    uint32_t                imageCapacity_ = 0;
    uint32_t                storageCapacity_ = 0;
    uint32_t                storageCount_    = 0;
};
