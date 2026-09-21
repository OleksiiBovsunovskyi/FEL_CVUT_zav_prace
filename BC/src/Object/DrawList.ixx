module;
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

export module DrawList;

export import GPUTypes;

export class DrawList;

/**
 * Owns one entry in a DrawList and removes it on destruction.
 *
 * Move-only: an entry has exactly one owner, and that owner is what decides
 * when the geometry stops existing.
 */
export class DrawHandle {
public:
    DrawHandle() = default;

    DrawHandle(const DrawHandle&)            = delete;
    DrawHandle& operator=(const DrawHandle&) = delete;

    DrawHandle(DrawHandle&& other) noexcept { swap(other); }
    DrawHandle& operator=(DrawHandle&& other) noexcept {
        if (this != &other) {
            release();
            swap(other);
        }
        return *this;
    }
    ~DrawHandle() { release(); }

    /**
     * Updates transform of the entry and marks it as changed
     * @param transform new transform to set for the entry
     */
    void setTransform(const glm::mat4& transform);

    /// @return true while this owns an entry.
    [[nodiscard]] explicit operator bool() const { return list_ != nullptr; }

private:
    friend class DrawList;

    DrawHandle(DrawList& list, uint32_t slot) : list_(&list), slot_(slot) {}

    void release();
    void swap(DrawHandle& other) noexcept {
        std::swap(list_, other.list_);
        std::swap(slot_, other.slot_);
    }

    DrawList* list_ = nullptr;
    uint32_t  slot_ = 0;
};

/**
 * Every registered instance, in the layout the mesh-draw buffers take.
 *
 * Entries outlive frames: a component registers once and keeps its handle, so
 * a frame uploads only what DrawList::takeChangedIndices() reports. Holds no
 * Vulkan object and knows no pass.
 */
export class DrawList {
public:
    DrawList() = default;

    /**
     * Every entry must be gone by now: each one is owned by a DrawHandle that
     * removes it, so a leftover means a handle is about to write to a dead
     * list.
     */
    ~DrawList();

    DrawList(const DrawList&)            = delete;
    DrawList& operator=(const DrawList&) = delete;

    /**
     * @param instance the record the mesh-draw buffers receive.
     * @param boundingSphere xyz = center, w = radius, in mesh space.
     * @return the handle that keeps the entry alive.
     */
    [[nodiscard]] DrawHandle add(const GPUMeshInstance& instance,
                                 const glm::vec4& boundingSphere);

    /// Dense, and not in registration order. Valid until the next add/remove.
    [[nodiscard]] std::span<const GPUMeshInstance> getItems() const { return items_; }

    /**
     * @param index Items index in the draw list
     * @return its bounding sphere in mesh space; xyz = center, w = radius.
     */
    [[nodiscard]] const glm::vec4& getItemBoundingSphere(uint32_t index) const {
        return boundingSpheres_[index];
    }

    /**
     * Indices of Draw list items changed since the previous call.
     *
     * @note Unsorted, may repeat an index, and may name an index past the end
     *       when the entry was removed after being written. 
     */
    [[nodiscard]] std::span<const uint32_t> takeChangedIndices();

    [[nodiscard]] std::size_t getItemsCount() const { return items_.size(); }
    [[nodiscard]] bool isEmpty() const { return items_.empty(); }

private:
    friend class DrawHandle;

    void remove(uint32_t slot);
    void setTransform(uint32_t slot, const glm::mat4& transform);

    /* Dense storage plus a slot map: the upload stays contiguous, removal is a
     * swap with the last entry, and the map is what keeps handles valid across
     * that swap. */
    std::vector<GPUMeshInstance> items_;
    /// Parallel to items_.
    std::vector<glm::vec4>       boundingSpheres_;
    std::vector<uint32_t>        slotToIndex_;
    std::vector<uint32_t>        indexToSlot_;
    std::vector<uint32_t>        freeSlots_;
    
    std::vector<uint32_t> changedIndices_;
    std::vector<uint32_t> consumedIndices_;
};
