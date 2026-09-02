module;
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

export module DrawList;

export import MultiMesh;

/**
 * One registered piece of geometry, at the world transform of whatever
 * registered it. Each part's own transform composes on top of it.
 */
export struct DrawItem {
    std::shared_ptr<MultiMesh> multiMesh;
    glm::mat4                  transform{1.0f};
    /// Per-frame switch; a hidden entry stays registered.
    bool                       isVisible = true;
};

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

    void setTransform(const glm::mat4& transform);
    void setVisible(bool visible);

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
 * Everything registered as drawable, in one contiguous array.
 *
 * Entries outlive frames: a component registers once and keeps its handle, so
 * a frame costs one scan and no rebuilding. Holds no Vulkan object and knows no
 * pass - an entry is geometry and a place to put it.
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

    /// @return the handle that keeps the entry alive.
    [[nodiscard]] DrawHandle add(DrawItem item);

    /// Dense, and not in registration order. Valid until the next add/remove.
    [[nodiscard]] std::span<const DrawItem> getItems() const { return items_; }

    [[nodiscard]] std::size_t getItemsCount() const { return items_.size(); }
    [[nodiscard]] bool isEmpty() const { return items_.empty(); }

private:
    friend class DrawHandle;

    void remove(uint32_t slot);
    [[nodiscard]] DrawItem& at(uint32_t slot) { return items_[slotToIndex_[slot]]; }

    /* Dense storage plus a slot map: the scan stays contiguous, removal is a
     * swap with the last entry, and the map is what keeps handles valid across
     * that swap. */
    std::vector<DrawItem> items_;
    std::vector<uint32_t> slotToIndex_;
    std::vector<uint32_t> indexToSlot_;
    std::vector<uint32_t> freeSlots_;
};
