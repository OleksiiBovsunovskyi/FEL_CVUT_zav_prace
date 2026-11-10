module;
#include <cstdint>
#include <string>
#include <span>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module DrawList;

import Logger;

DrawList::~DrawList() {
    if (!items_.empty())
        logError("DrawList: destroyed with " + std::to_string(items_.size()) +
                 " entries left; whatever owns them will write to a dead list. "
                 "Declare the DrawList before what registers in it.");
}

namespace {

/// Writes Instance::position, scale and rotation.
void packTransform(Instance& instance, const Transform& transform) {
    instance.position = transform.position;
    instance.packScale(transform.scale);
    instance.packRotation(transform.rotation);
}

} // namespace

DrawHandle DrawList::add(GpuPtr<Mesh> mesh, const Transform& transform,
                         const glm::vec4& boundingSphere) {
    Instance instance{};
    instance.mesh = mesh;
    packTransform(instance, transform);

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

    items_.push_back(instance);
    boundingSpheres_.push_back(boundingSphere);
    indexToSlot_.push_back(slot);
    changedIndices_.push_back(index);

    return DrawHandle{*this, slot};
}

void DrawList::remove(uint32_t slot) {
    const uint32_t index = slotToIndex_[slot];
    const auto     last  = static_cast<uint32_t>(items_.size() - 1);

    /* Swap with the last entry so the upload stays dense, then repoint
     * whichever slot owned that entry. */
    if (index != last) {
        items_[index]            = items_[last];
        boundingSpheres_[index]  = boundingSpheres_[last];
        indexToSlot_[index]      = indexToSlot_[last];
        slotToIndex_[indexToSlot_[index]] = index;
        changedIndices_.push_back(index);
    }

    items_.pop_back();
    boundingSpheres_.pop_back();
    indexToSlot_.pop_back();
    freeSlots_.push_back(slot);
}

void DrawList::setTransform(uint32_t slot, const Transform& transform) {
    const uint32_t index = slotToIndex_[slot];
    packTransform(items_[index], transform);
    changedIndices_.push_back(index);
}

std::span<const uint32_t> DrawList::takeChangedIndices() {
    consumedIndices_.clear();
    consumedIndices_.swap(changedIndices_);
    return consumedIndices_;
}

void DrawHandle::release() {
    if (list_) list_->remove(slot_);
    list_ = nullptr;
    slot_ = 0;
}

void DrawHandle::setTransform(const Transform& transform) {
    if (list_) list_->setTransform(slot_, transform);
}
