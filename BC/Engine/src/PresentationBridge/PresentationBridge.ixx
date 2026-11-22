module;

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

export module PresentationBridge;

import vulkan;

/// Exported Win32 NT handles for one GPU-shared render target slot.
export struct PgrExternalSlotHandles {
    void*    memoryHandle;
    uint64_t memorySize;
    uint64_t memoryOffset;
    void*    renderDoneSemaphoreHandle;
    void*    avaloniaDoneSemaphoreHandle;
};

/// Set of GPU-shared target slots created for one viewport extent.
export struct PgrTargetSet {
    uint32_t                      slotCount;
    uint32_t                      width;
    uint32_t                      height;
    const PgrExternalSlotHandles* slots;
};

/// Host-supplied callbacks invoked from the native render thread.
export struct PgrHostCallbacks {
    void* userData;
    void (*onTargetsCreated)(void* userData, const PgrTargetSet* targets);
    void (*onTargetsDestroyed)(void* userData);
    void (*onPresent)(void* userData, uint32_t slotIndex);
    void (*onError)(void* userData, const char* message);
};

/// Startup parameters passed across the C ABI into pgr_start().
export struct PgrStartConfig {
    const char*      dataRoot;
    uint32_t         width;
    uint32_t         height;
    const uint8_t*   deviceUuid;
    PgrHostCallbacks callbacks;
};

export struct KeyReport {
    unsigned char ascii;
    bool          down;
};

export struct MouseDelta {
    float dx = 0.0f;
    float dy = 0.0f;
};

export struct AcquiredSlot {
    uint32_t index;
    bool     hasBeenPresented;
};

/**
 * Thread-safe bridge between an external UI host and the engine's render thread.
 * Owns the queued key events, accumulated mouse delta, pending resize, and slot availability.
 */
export class PresentationBridge {
public:
    explicit PresentationBridge(PgrHostCallbacks callbacks) : callbacks_(callbacks) {}

    PresentationBridge(const PresentationBridge&)            = delete;
    PresentationBridge& operator=(const PresentationBridge&) = delete;

    void pushKey(unsigned char ascii, bool down);
    void pushMouseMove(float dx, float dy);
    void takeInput(std::vector<KeyReport>& keys, MouseDelta& mouse);

    void requestResize(vk::Extent2D extent);
    [[nodiscard]] std::optional<vk::Extent2D> requestedExtent() const;

    void requestStop();
    [[nodiscard]] bool stopRequested() const;

    void resetSlots(uint32_t count);
    [[nodiscard]] std::optional<AcquiredSlot> acquireSlot();
    void presentSlot(uint32_t slotIndex);
    void releaseSlot(uint32_t slotIndex);

    void notifyTargetsCreated(const PgrTargetSet& targets) const;
    void notifyTargetsDestroyed() const;
    void notifyError(const std::string& message) const;

    static void setLastError(std::string message);
    [[nodiscard]] static const char* lastError();

private:
    PgrHostCallbacks callbacks_{};

    mutable std::mutex     inputMutex_;
    std::vector<KeyReport> pendingKeys_;
    MouseDelta             pendingMouse_{};
    std::optional<vk::Extent2D> requestedExtent_;

    mutable std::mutex      slotMutex_;
    std::condition_variable slotCv_;
    std::vector<bool>       slotFree_;
    std::vector<bool>       slotPresented_;
    uint32_t                nextSlot_      = 0;
    bool                    stopRequested_ = false;
};

/// Opaque handle returned by pgr_start() and destroyed by pgr_stop().
export struct AppHandle {
    PresentationBridge bridge;
    std::thread        worker;

    explicit AppHandle(PgrHostCallbacks callbacks) : bridge(callbacks) {}
};
