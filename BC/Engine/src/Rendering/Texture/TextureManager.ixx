module;

#include <cassert>
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
import RangeAllocator;
import Releaser;
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
 * Consecutive storage slots handed out by one TextureManager::addStorage() call.
 * View n of that call is in slot first.value + n.
 */
export struct StorageRun {
    StorageSlot first{};
    uint32_t    count = 0;

    /// @return slot of view `index` of the run.
    [[nodiscard]] StorageSlot operator[](uint32_t index) const {
        assert(index < count && "StorageRun: index past the run");
        return StorageSlot{first.value + index};
    }

    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(first); }
};

/**
 * Owns the bindless image arrays, the sampler and the descriptor set they live in.
 * Binding 0: sampled images, `Sampler2D textures[]`.
 * Binding 1: storage images in eGeneral, `RWTexture2D storageImages[]`, compute only.
 * Owns the images of slots handed out by TextureManager::add(); those slots are never freed.
 * Borrows the images of slots handed out by TextureManager::addSampledBorrowed() and
 * TextureManager::addStorage().
 * Retired borrowed slots are freed by the Releaser passed to TextureManager::init().
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
     * @param releaser runs the frees of retired slots; must outlive this.
     * @return false after logging, leaving nothing allocated.
     * @note Call before Renderer::init: a pipeline layout built from layout()
     *       before this runs gets a null handle and no descriptor set.
     */
    [[nodiscard]] bool init(VulkanContext& ctx, Releaser& releaser);

    /// @note Every slot retired through the Releaser must be freed before this is called.
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
     * Rewrites a slot from TextureManager::addSampledBorrowed() with a new view.
     * @param slot slot returned by TextureManager::addSampledBorrowed().
     * @param view view the shaders sample.
     * @param layout layout the image is in whenever a shader reads the slot.
     * @return false after logging when the slot is not a live borrowed one.
     * @note The GPU must be done with the slot before this is called.
     */
    [[nodiscard]] bool replaceSampledBorrowed(TextureSlot slot, ImageRef view, vk::ImageLayout layout);

    /**
     * Gives a slot from TextureManager::addSampledBorrowed() back.
     * @param slot slot returned by TextureManager::addSampledBorrowed().
     * @note The slot is handed out again once the Releaser runs its free.
     * @note Does nothing after TextureManager::destroy().
     */
    void retireSampledBorrowed(TextureSlot slot);

    /**
     * Writes views of an image owned elsewhere into consecutive new storage slots.
     * @param views views the shaders write, in slot order.
     * @return the run, or an empty StorageRun after logging.
     * @note The image must be in eGeneral whenever a shader accesses a slot of the run.
     * @note Borrowed image will not be owned by this class.
     */
    [[nodiscard]] StorageRun addStorage(std::span<const ImageRef> views);

    /**
     * Rewrites one slot of a run from TextureManager::addStorage() with a new view.
     * @param slot slot inside a live run.
     * @param view view the shaders write.
     * @return false after logging when the slot is not inside a live run.
     * @note The GPU must be done with the slot before this is called.
     */
    [[nodiscard]] bool replaceStorage(StorageSlot slot, ImageRef view);

    /**
     * Gives a run from TextureManager::addStorage() back.
     * @param run run returned by TextureManager::addStorage().
     * @note The run is handed out again once the Releaser runs its free.
     * @note Does nothing after TextureManager::destroy().
     */
    void retireStorage(StorageRun run);

    /// @return slots the sampled array holds, the device limit read by init().
    [[nodiscard]] uint32_t capacity() const { return imageCapacity_; }

    /// @return slots the storage array holds.
    [[nodiscard]] uint32_t storageCapacity() const { return storageCapacity_; }

    [[nodiscard]] vk::DescriptorSetLayout layout() const { return layout_; }
    [[nodiscard]] vk::DescriptorSet       set()    const { return set_; }

    /// @return sampled slots handed out or retired and not yet reused.
    [[nodiscard]] uint32_t count() const { return static_cast<uint32_t>(sampledSlots_.used()); }

    /// @return storage slots handed out or retired and not yet reused.
    [[nodiscard]] uint32_t storageCount() const { return static_cast<uint32_t>(storageSlots_.used()); }

    /// Storage array slots; sized for the mip views of render targets.
    static constexpr uint32_t MAX_STORAGE_IMAGES = 1024;

private:
    /// One handed-out sampled slot.
    struct SampledSlotRecord {
        RangeAllocation range{};
        bool            borrowed = false;
    };

    /// Hands the free of `range` in `slots` to the Releaser.
    void retireRange(RangeAllocator& slots, const RangeAllocation& range);

    /// @return the slot, or an empty TextureSlot after logging when the array is full.
    [[nodiscard]] TextureSlot reserveSlot(bool borrowed);

    /// Frees a slot from TextureManager::reserveSlot() that no shader has seen.
    void unreserveSlot(TextureSlot slot);

    /// @return the record of a live slot, or nullptr.
    [[nodiscard]] SampledSlotRecord* sampledRecord(TextureSlot slot);

    /// @return the run holding a live slot, or nullptr.
    [[nodiscard]] const RangeAllocation* storageRunOf(StorageSlot slot) const;

    void writeSampledDescriptor(TextureSlot slot, ImageRef view, vk::ImageLayout layout);
    void writeStorageDescriptor(StorageSlot slot, ImageRef view);

    /// Stable addresses: Image is not movable.
    std::deque<Image> images_;

    /// Units are slots of binding 0.
    RangeAllocator sampledSlots_;
    /// Units are slots of binding 1.
    RangeAllocator storageSlots_;

    /// Indexed by TextureSlot::value; grows to the highest slot handed out.
    std::vector<SampledSlotRecord> sampledRecords_;
    /// Indexed by StorageSlot::value; sized to storageCapacity_; a live run's range on every slot it covers.
    std::vector<RangeAllocation> storageRuns_;

    Releaser* releaser_ = nullptr;
    /// Frees handed to releaser_ and not yet run.
    uint32_t  pendingReleases_ = 0;

    vk::Device              device_  = nullptr;
    vma::Allocator          allocator_ = nullptr;
    vk::Sampler             sampler_ = nullptr;
    vk::DescriptorSetLayout layout_  = nullptr;
    vk::DescriptorPool      pool_    = nullptr;
    vk::DescriptorSet       set_     = nullptr;
    uint32_t                imageCapacity_ = 0;
    uint32_t                storageCapacity_ = 0;
};
