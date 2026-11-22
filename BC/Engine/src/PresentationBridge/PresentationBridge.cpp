module;

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module PresentationBridge;

#define ENGINE_API extern "C" __declspec(dllexport)

namespace {

thread_local std::string tlsLastError;

} // namespace

void PresentationBridge::pushKey(unsigned char ascii, bool down) {
    std::lock_guard lock(inputMutex_);
    pendingKeys_.push_back({ascii, down});
}

void PresentationBridge::pushMouseMove(float dx, float dy) {
    std::lock_guard lock(inputMutex_);
    pendingMouse_.dx += dx;
    pendingMouse_.dy += dy;
}

void PresentationBridge::takeInput(std::vector<KeyReport>& keys, MouseDelta& mouse) {
    std::lock_guard lock(inputMutex_);
    keys.swap(pendingKeys_);
    pendingKeys_.clear();
    mouse         = pendingMouse_;
    pendingMouse_ = {};
}

void PresentationBridge::requestResize(vk::Extent2D extent) {
    if (extent.width == 0 || extent.height == 0) return;
    std::lock_guard lock(inputMutex_);
    requestedExtent_ = extent;
}

std::optional<vk::Extent2D> PresentationBridge::requestedExtent() const {
    std::lock_guard lock(inputMutex_);
    return requestedExtent_;
}

void PresentationBridge::requestStop() {
    {
        std::lock_guard lock(slotMutex_);
        stopRequested_ = true;
    }
    slotCv_.notify_all();
}

bool PresentationBridge::stopRequested() const {
    std::lock_guard lock(slotMutex_);
    return stopRequested_;
}

void PresentationBridge::resetSlots(uint32_t count) {
    {
        std::lock_guard lock(slotMutex_);
        slotFree_.assign(count, true);
        slotPresented_.assign(count, false);
        nextSlot_ = 0;
    }
    slotCv_.notify_all();
}

std::optional<AcquiredSlot> PresentationBridge::acquireSlot() {
    std::unique_lock lock(slotMutex_);
    slotCv_.wait(lock, [this] {
        return stopRequested_ || (!slotFree_.empty() && slotFree_[nextSlot_]);
    });
    if (stopRequested_) return std::nullopt;

    const uint32_t slot = nextSlot_;
    slotFree_[slot]     = false;
    nextSlot_           = (nextSlot_ + 1) % static_cast<uint32_t>(slotFree_.size());
    return AcquiredSlot{.index = slot, .hasBeenPresented = slotPresented_[slot]};
}

void PresentationBridge::presentSlot(uint32_t slotIndex) {
    {
        std::lock_guard lock(slotMutex_);
        if (slotIndex < slotPresented_.size()) slotPresented_[slotIndex] = true;
    }
    if (callbacks_.onPresent)
        callbacks_.onPresent(callbacks_.userData, slotIndex);
    else
        releaseSlot(slotIndex);
}

void PresentationBridge::releaseSlot(uint32_t slotIndex) {
    {
        std::lock_guard lock(slotMutex_);
        if (slotIndex < slotFree_.size()) slotFree_[slotIndex] = true;
    }
    slotCv_.notify_one();
}

void PresentationBridge::notifyTargetsCreated(const EngineTargetSet& targets) const {
    if (callbacks_.onTargetsCreated)
        callbacks_.onTargetsCreated(callbacks_.userData, &targets);
}

void PresentationBridge::notifyTargetsDestroyed() const {
    if (callbacks_.onTargetsDestroyed)
        callbacks_.onTargetsDestroyed(callbacks_.userData);
}

void PresentationBridge::notifyError(const std::string& message) const {
    setLastError(message);
    if (callbacks_.onError)
        callbacks_.onError(callbacks_.userData, message.c_str());
}

void PresentationBridge::setLastError(std::string message) {
    tlsLastError = std::move(message);
}

const char* PresentationBridge::lastError() {
    return tlsLastError.c_str();
}

//Engine API for the presentation layer
ENGINE_API void engine_stop(AppHandle* app) {
    if (!app) return;
    try {
        app->bridge.requestStop();
        if (app->worker.joinable()) app->worker.join();
        delete app;
    } catch (...) {
    }
}

ENGINE_API void engine_release_slot(AppHandle* app, uint32_t slotIndex) {
    if (app) app->bridge.releaseSlot(slotIndex);
}

ENGINE_API void engine_resize(AppHandle* app, uint32_t width, uint32_t height) {
    if (app && width > 0 && height > 0)
        app->bridge.requestResize({width, height});
}

ENGINE_API void engine_key(AppHandle* app, uint8_t ascii, int down) {
    if (app) app->bridge.pushKey(ascii, down != 0);
}

ENGINE_API void engine_mouse_move(AppHandle* app, float dx, float dy) {
    if (app) app->bridge.pushMouseMove(dx, dy);
}

ENGINE_API const char* engine_last_error() {
    return PresentationBridge::lastError();
}
