module;

#include <bit>
#include <cassert>
#include <cstdint>
#include <utility>

export module RangeAllocator;

import vulkan;
import vk_mem_alloc;

/**
 * One range reserved in a RangeAllocator.
 * The range is not owned; RangeAllocator::free() releases it.
 */
export struct RangeAllocation {
    uint64_t               offset = 0;
    uint64_t               size   = 0;
    vma::VirtualAllocation handle = nullptr;

    [[nodiscard]] explicit operator bool() const { return handle != nullptr; }
};

/**
 * Hands out ranges of [0, capacity) in units the owner defines.
 * Backed by a VMA virtual block; owns no memory.
 * The block is destroyed on destruction.
 */
export class RangeAllocator {
public:
    RangeAllocator() = default;
    ~RangeAllocator() { destroy(); }

    RangeAllocator(const RangeAllocator&)            = delete;
    RangeAllocator& operator=(const RangeAllocator&) = delete;

    RangeAllocator(RangeAllocator&& other) noexcept { std::swap(block_, other.block_); }
    RangeAllocator& operator=(RangeAllocator&& other) noexcept {
        if (this != &other) {
            destroy();
            std::swap(block_, other.block_);
        }
        return *this;
    }

    /**
     * Creates the block.
     * @param capacity units the block covers.
     * @return false when the block could not be created.
     */
    [[nodiscard]] bool init(uint64_t capacity) {
        assert(!block_ && "RangeAllocator::init: called twice");
        vma::VirtualBlockCreateInfo info{};
        info.size = capacity;
        return vma::createVirtualBlock(&info, &block_) == vk::Result::eSuccess;
    }

    /// Releases every range and destroys the block. Safe to call twice.
    void destroy() {
        if (!block_) return;
        block_.clearVirtualBlock();
        block_.destroy();
        block_ = nullptr;
    }

    /**
     * Reserves a range.
     * @param size units to reserve.
     * @param alignment in units; must be a power of two.
     * @return the range, empty when no free range is large enough.
     */
    [[nodiscard]] RangeAllocation allocate(uint64_t size, uint64_t alignment = 1) {
        assert(std::has_single_bit(alignment) && "RangeAllocator::allocate: alignment is not a power of two");
        if (!block_ || size == 0) return {};

        vma::VirtualAllocationCreateInfo info{};
        info.size      = size;
        info.alignment = alignment;

        RangeAllocation range{.size = size};
        if (block_.virtualAllocate(&info, &range.handle, &range.offset) != vk::Result::eSuccess)
            return {};
        return range;
    }

    /**
     * Releases one range.
     * @param range range from RangeAllocator::allocate().
     */
    void free(const RangeAllocation& range) {
        if (block_ && range) block_.virtualFree(range.handle);
    }

    /// Releases every range at once.
    void clear() {
        if (block_) block_.clearVirtualBlock();
    }

    /// @return units currently reserved.
    [[nodiscard]] uint64_t used() const {
        if (!block_) return 0;
        return block_.getVirtualBlockStatistics().allocationBytes;
    }

    [[nodiscard]] explicit operator bool() const { return block_ != nullptr; }

private:
    vma::VirtualBlock block_ = nullptr;
};
