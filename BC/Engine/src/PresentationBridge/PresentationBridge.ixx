module;

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

export module PresentationBridge;

import vulkan;

/// Exported Win32 NT handles for one GPU-shared render target slot.
export struct EngineExternalSlotHandles {
    void*    memoryHandle;
    uint64_t memorySize;
    uint64_t memoryOffset;
    void*    renderDoneSemaphoreHandle;
    void*    avaloniaDoneSemaphoreHandle;
};

/// Set of GPU-shared target slots created for one viewport extent.
export struct EngineTargetSet {
    uint32_t                         slotCount;
    uint32_t                         width;
    uint32_t                         height;
    const EngineExternalSlotHandles* slots;
};

/// One named per-frame measurement transferred across the C ABI.
export struct EngineMetricEntry {
    const char* name;
    double      value;
};

/// Completed frame's measurements passed to EngineHostCallbacks::onMetrics.
export struct EngineFrameMetrics {
    uint32_t                 count;
    const EngineMetricEntry* entries;
};

/// Bit positions inside EngineDebugFlags::bits.
export enum EngineDebugFlagBit : uint32_t {
    ENGINE_DEBUG_SHOW_MESHLETS        = 1u << 0,
    ENGINE_DEBUG_SHOW_MESHLET_SPHERES = 1u << 1,
    ENGINE_DEBUG_DRAW_NORMALS         = 1u << 2,
    ENGINE_DEBUG_USE_HIZ_OC           = 1u << 3,
    ENGINE_DEBUG_SHOW_VERTEX_POSITION_SIZE = 1u << 4,
};

/// Renderer debug toggles passed across the C ABI by engine_set_debug_flags().
export struct EngineDebugFlags {
    uint32_t bits;

    [[nodiscard]] bool has(EngineDebugFlagBit bit) const { return (bits & bit) != 0; }
};

/// Host-supplied callbacks invoked from the native render thread.
export struct EngineHostCallbacks {
    void* userData;
    void (*onTargetsCreated)(void* userData, const EngineTargetSet* targets);
    void (*onTargetsDestroyed)(void* userData);
    void (*onPresent)(void* userData, uint32_t slotIndex);
    void (*onError)(void* userData, const char* message);
    void (*onMetrics)(void* userData, const EngineFrameMetrics* metrics);
};

/// Startup parameters passed across the C ABI into app_start().
export struct AppStartConfig {
    const char*         dataRoot;
    uint32_t            width;
    uint32_t            height;
    const uint8_t*      deviceUuid;
    EngineHostCallbacks callbacks;
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
    explicit PresentationBridge(EngineHostCallbacks callbacks) : callbacks_(callbacks) {}

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

    void notifyTargetsCreated(const EngineTargetSet& targets) const;
    void notifyTargetsDestroyed() const;
    void notifyError(const std::string& message) const;
    void notifyMetrics(std::span<const EngineMetricEntry> entries) const;

    void requestDebugFlags(EngineDebugFlags flags);
    /**
     * Takes the debug flags requested since the last call.
     * @return std::nullopt when no request arrived since the last call.
     */
    [[nodiscard]] std::optional<EngineDebugFlags> takeDebugFlags();

    static void setLastError(std::string message);
    [[nodiscard]] static const char* lastError();

private:
    EngineHostCallbacks callbacks_{};

    mutable std::mutex     inputMutex_;
    std::vector<KeyReport> pendingKeys_;
    MouseDelta             pendingMouse_{};
    std::optional<vk::Extent2D> requestedExtent_;
    std::optional<EngineDebugFlags> pendingDebugFlags_;

    mutable std::mutex      slotMutex_;
    std::condition_variable slotCv_;
    std::vector<bool>       slotFree_;
    std::vector<bool>       slotPresented_;
    uint32_t                nextSlot_      = 0;
    bool                    stopRequested_ = false;
};

/// Opaque handle returned by app_start() and destroyed by engine_stop().
export struct AppHandle {
    PresentationBridge bridge;
    std::thread        worker;

    explicit AppHandle(EngineHostCallbacks callbacks) : bridge(callbacks) {}
};
