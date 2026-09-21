module;

#include <cstdint>
#include <deque>
#include <span>

export module TextureManager;

import vulkan;
import vk_mem_alloc;
import VulkanContext;
import BufferManager;
import Image;
export import GPUTypes;

/**
 * Owns every sampled texture, the sampler, and the descriptor set they are
 * reached through.
 *
 * TextureManager::add() hands out the slots.
 * A slot index is baked into a GPUMaterial and outlives the Material
 * that asked for it.
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
     * @return the slot, or INVALID_TEXTURE_INDEX after logging.
     * @note Mip levels are not generated here; a texture arrives with the chain
     *       its baker built.
     * @note The image is left in eShaderReadOnlyOptimal once the batch runs.
     * @note The descriptor is written immediately, which the set's
     *       eUpdateAfterBind flag permits while it is bound elsewhere.
     */
    [[nodiscard]] uint32_t add(BufferManager& buffers, vk::CommandBuffer commandBuffer,
                               std::span<const std::byte> levels, vk::Format format,
                               vk::Extent2D extent, uint32_t levelCount);

    /// @return slots the bindless array holds, the device limit read by init().
    [[nodiscard]] uint32_t capacity() const { return capacity_; }

    [[nodiscard]] vk::DescriptorSetLayout layout() const { return layout_; }
    [[nodiscard]] vk::DescriptorSet       set()    const { return set_; }

    /// @return slots handed out so far.
    [[nodiscard]] uint32_t count() const { return static_cast<uint32_t>(images_.size()); }

private:
    /// Stable addresses: Image is not movable.
    std::deque<Image> images_;

    vk::Device              device_  = nullptr;
    vma::Allocator          allocator_ = nullptr;
    vk::Sampler             sampler_ = nullptr;
    vk::DescriptorSetLayout layout_  = nullptr;
    vk::DescriptorPool      pool_    = nullptr;
    vk::DescriptorSet       set_     = nullptr;
    uint32_t                capacity_ = 0;
};
