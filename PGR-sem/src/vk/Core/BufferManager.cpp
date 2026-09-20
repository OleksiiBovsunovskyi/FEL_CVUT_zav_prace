module;

#include <array>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

module BufferManager;

import vulkan;
import vk_mem_alloc;
import GPUTypes;
import Logger;

namespace {

constexpr size_t index(StaticBufferKind kind) {
    return static_cast<size_t>(kind);
}

/// Runtime form of the traits, for the loops that walk every buffer.
struct BufferSpec {
    const char*          name  = "";
    vk::BufferUsageFlags usage = {};
};

/* Instantiating over the whole enum is what makes a missing traits row a
 * compile error instead of a buffer that is silently never created. */
template <template <auto> class Traits, typename Kind, size_t... Index>
constexpr auto specsOf(std::index_sequence<Index...>) {
    static_assert(
        (std::is_same_v<typename Traits<static_cast<Kind>(Index)>::Buffer,
                        DeviceOnlyBuffer> && ...),
        "every static buffer kind must be a DeviceOnlyBuffer; "
        "BufferManager holds them in one array of that placement");
    return std::array<BufferSpec, sizeof...(Index)>{
        BufferSpec{Traits<static_cast<Kind>(Index)>::name,
                   Traits<static_cast<Kind>(Index)>::usage}...};
}

constexpr auto STATIC_SPECS = specsOf<StaticBufferTraits, StaticBufferKind>(
    std::make_index_sequence<STATIC_BUFFER_COUNT>{});

constexpr const char*        UPLOAD_NAME  = "upload";
constexpr vk::BufferUsageFlags UPLOAD_USAGE = vk::BufferUsageFlagBits::eTransferSrc;

} // namespace

BufferManager::~BufferManager() {
    if (initialized()) {
        logError("BufferManager: destroyed without shutdown() - the mega-buffers "
                 "have leaked. Call shutdown() while the device is still alive.");
    }
}

bool BufferManager::init(VulkanContext& ctx, const GPUBufferCapacities& gpuBufferCapacities) {
    if (initialized()) {
        logError("BufferManager: init called twice");
        return false;
    }

    for (size_t i = 0; i < STATIC_BUFFER_COUNT; ++i) {
        if (gpuBufferCapacities.staticBytes[i] == 0) continue;   // opted out

        if (!staticBuffers_[i].init(ctx, gpuBufferCapacities.staticBytes[i],
                                    STATIC_SPECS[i].usage, STATIC_SPECS[i].name)) {
            shutdown();
            return false;
        }
    }

    if (!upload_.init(ctx, gpuBufferCapacities.upload, UPLOAD_USAGE, UPLOAD_NAME)) {
        shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

void BufferManager::shutdown() {
    retired_.clear();
    upload_.destroy();

    for (auto& buffer : staticBuffers_)
        buffer.destroy();

    retirementSerial_ = 0;
    initialized_      = false;
}

SubAllocationHandle BufferManager::allocateStaticRaw(
    StaticBufferKind kind, vk::DeviceSize bytes, vk::DeviceSize alignment) {
    if (kind == StaticBufferKind::Count) return {};

    SubAllocationHandle allocation = staticBuffers_[index(kind)].allocate(bytes, alignment);
    if (!allocation)
        logError(std::string("BufferManager: ") + STATIC_SPECS[index(kind)].name +
                 " is full");
    return allocation;
}

MappedSpan<std::byte> BufferManager::allocateUpload(vk::DeviceSize bytes,
                                                 vk::DeviceSize alignment) {
    if (bytes > upload_.capacity()) {
        logError("BufferManager: upload request of " + std::to_string(bytes) +
                 " bytes exceeds the whole " + std::to_string(upload_.capacity()) +
                 " byte upload buffer; resetUpload() will not help, raise "
                 "GPUBufferCapacities::upload or split the transfer");
        return {};
    }

    const SubAllocationHandle allocation = upload_.allocate(bytes, alignment);
    if (!allocation) {
        logError("BufferManager: upload buffer is full; submit the open transfer batch "
                 "to reclaim it");
        return {};
    }

    return UploadBuffer::spanOf<std::byte>(allocation,
                                           static_cast<uint32_t>(bytes));
}

GpuPtr<std::byte> BufferManager::staticBaseAddress(StaticBufferKind kind) const {
    if (kind == StaticBufferKind::Count) return {};
    return staticBuffers_[index(kind)].gpuAddress<std::byte>();
}

BufferUsage BufferManager::staticUsage(StaticBufferKind kind) const {
    if (kind == StaticBufferKind::Count) return {};

    const StaticBuffer& buffer = staticBuffers_[index(kind)];
    return BufferUsage{STATIC_SPECS[index(kind)].name, buffer.capacity(),
                       buffer.used()};
}

void BufferManager::retire(StaticBufferKind kind, const SubAllocationHandle& allocation) {
    if (!initialized() || kind == StaticBufferKind::Count || !allocation) return;
    retired_.push_back({kind, allocation, retirementSerial_});
}

void BufferManager::collect(uint64_t completedSerial) {
    auto out = retired_.begin();
    for (auto it = retired_.begin(); it != retired_.end(); ++it) {
        if (it->serial <= completedSerial)
            staticBuffers_[index(it->kind)].free(it->allocation);
        else
            *out++ = *it;
    }
    retired_.erase(out, retired_.end());
}

void BufferManager::resetUpload() {
    upload_.clear();
}
