module;
#include <cstdint>
#include <string>
#include <utility>

#include <glm/glm.hpp>

module DrawList;

import Logger;

DrawList::~DrawList() {
    if (!items_.empty())
        logError("DrawList: destroyed with " + std::to_string(items_.size()) +
                 " entries left; whatever owns them will write to a dead list. "
                 "Declare the DrawList before what registers in it.");
}

DrawHandle DrawList::add(DrawItem item) {
    const auto index = static_cast<uint32_t>(items_.size());

    uint32_t slot = 0;
    if (freeSlots_.empty()) {
        slot = static_cast<uint32_t>(slotToIndex_.size());
        slotToIndex_.push_back(index);
    } else {
        slot = freeSlots_.back();
        freeSlots_.pop_back();
        slotToIndex_[slot] = index;
    }

    items_.push_back(std::move(item));
    indexToSlot_.push_back(slot);

    return DrawHandle{*this, slot};
}

void DrawList::remove(uint32_t slot) {
    const uint32_t index = slotToIndex_[slot];
    const auto     last  = static_cast<uint32_t>(items_.size() - 1);

    /* Swap with the last entry so the scan stays dense, then repoint whichever
     * slot owned that entry. */
    if (index != last) {
        items_[index]       = std::move(items_[last]);
        indexToSlot_[index] = indexToSlot_[last];
        slotToIndex_[indexToSlot_[index]] = index;
    }

    items_.pop_back();
    indexToSlot_.pop_back();
    freeSlots_.push_back(slot);
}

void DrawHandle::release() {
    if (list_) list_->remove(slot_);
    list_ = nullptr;
    slot_ = 0;
}

void DrawHandle::setTransform(const glm::mat4& transform) {
    if (list_) list_->at(slot_).transform = transform;
}

void DrawHandle::setVisible(bool visible) {
    if (list_) list_->at(slot_).isVisible = visible;
}
