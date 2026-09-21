module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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
    const char*          name       = "";
    vk::BufferUsageFlags usage      = {};
    vk::DeviceSize       blockBytes = 0;
    bool                 growable   = false;
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
                   Traits<static_cast<Kind>(Index)>::usage,
                   Traits<static_cast<Kind>(Index)>::blockBytes,
                   Traits<static_cast<Kind>(Index)>::growable}...};
}

constexpr auto STATIC_SPECS = specsOf<StaticBufferTraits, StaticBufferKind>(
    std::make_index_sequence<STATIC_BUFFER_COUNT>{});

constexpr const char*          UPLOAD_NAME  = "upload";
constexpr vk::BufferUsageFlags UPLOAD_USAGE = vk::BufferUsageFlagBits::eTransferSrc;
constexpr vk::DeviceSize       UPLOAD_BYTES = 512ull << 20;

} // namespace

BufferManager::~BufferManager() {
    if (initialized()) {
        logError("BufferManager: destroyed without shutdown() - the mega-buffers "
                 "have leaked. Call shutdown() while the device is still alive.");
    }
}

bool BufferManager::init(VulkanContext& ctx) {
    if (initialized()) {
        logError("BufferManager: init called twice");
        return false;
    }
    ctx_ = &ctx;

    /* A fixed-size kind is indexed from its base address, which has to answer
     * before anything is allocated. */
    for (size_t i = 0; i < STATIC_BUFFER_COUNT; ++i) {
        if (STATIC_SPECS[i].growable) continue;

        if (!addBlock(static_cast<StaticBufferKind>(i), STATIC_SPECS[i].blockBytes)) {
            shutdown();
            return false;
        }
    }

    if (!upload_.init(ctx, UPLOAD_BYTES, UPLOAD_USAGE, UPLOAD_NAME)) {
        shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

BufferManager::StaticBuffer* BufferManager::addBlock(StaticBufferKind kind,
                                                     vk::DeviceSize bytes) {
    if (!ctx_ || kind == StaticBufferKind::Count) return nullptr;

    const BufferSpec&          spec   = STATIC_SPECS[index(kind)];
    std::vector<StaticBuffer>& blocks = staticBuffers_[index(kind)];

    StaticBuffer block;
    if (!block.init(*ctx_, bytes, spec.usage, spec.name)) {
        logError(std::string("BufferManager: ") + spec.name + " could not take a " +
                 std::to_string(bytes >> 20) + " MiB block");
        return nullptr;
    }
    blocks.push_back(std::move(block));

    if (blocks.size() > 1)
        logMessage(std::string("BufferManager: ") + spec.name + " grew to " +
                   std::to_string(blocks.size()) + " blocks, " +
                   std::to_string(bytes >> 20) + " MiB added");
    return &blocks.back();
}

BufferManager::StaticBuffer* BufferManager::blockOf(StaticBufferKind kind,
                                                    GpuPtr<std::byte> address) {
    if (kind == StaticBufferKind::Count) return nullptr;

    for (StaticBuffer& block : staticBuffers_[index(kind)]) {
        const vk::DeviceAddress base = block.gpuAddress<std::byte>().address;
        if (address.address >= base && address.address < base + block.capacity())
            return &block;
    }
    return nullptr;
}

void BufferManager::shutdown() {
    retired_.clear();
    upload_.destroy();

    for (auto& blocks : staticBuffers_)
        blocks.clear();

    ctx_              = nullptr;
    retirementSerial_ = 0;
    initialized_      = false;
}

SubAllocationHandle BufferManager::allocateStaticRaw(
    StaticBufferKind kind, vk::DeviceSize bytes, vk::DeviceSize alignment) {
    if (kind == StaticBufferKind::Count || bytes == 0) return {};

    for (StaticBuffer& block : staticBuffers_[index(kind)])
        if (SubAllocationHandle allocation = block.allocate(bytes, alignment))
            return allocation;

    const BufferSpec& spec = STATIC_SPECS[index(kind)];
    if (!spec.growable) {
        logError(std::string("BufferManager: ") + spec.name + " is full");
        return {};
    }

    /* One allocation never spans two blocks, so an oversized one gets a block
     * of its own. */
    StaticBuffer* block = addBlock(kind, std::max(spec.blockBytes, bytes));
    if (!block) return {};

    SubAllocationHandle allocation = block->allocate(bytes, alignment);
    if (!allocation)
        logError(std::string("BufferManager: ") + spec.name +
                 " rejected an allocation from a block sized for it");
    return allocation;
}

MappedSpan<std::byte> BufferManager::allocateUpload(vk::DeviceSize bytes,
                                                 vk::DeviceSize alignment) {
    if (bytes > upload_.capacity()) {
        logError("BufferManager: upload request of " + std::to_string(bytes) +
                 " bytes exceeds the whole " + std::to_string(upload_.capacity()) +
                 " byte upload buffer; resetUpload() will not help, split "
                 "the transfer");
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

    const std::vector<StaticBuffer>& blocks = staticBuffers_[index(kind)];
    if (blocks.empty()) return {};
    return blocks.front().gpuAddress<std::byte>();
}

BufferUsage BufferManager::staticUsage(StaticBufferKind kind) const {
    if (kind == StaticBufferKind::Count) return {};

    BufferUsage usage{STATIC_SPECS[index(kind)].name};
    for (const StaticBuffer& block : staticBuffers_[index(kind)]) {
        usage.capacity += block.capacity();
        usage.used     += block.used();
        ++usage.blocks;
    }
    return usage;
}

void BufferManager::retire(StaticBufferKind kind, const SubAllocationHandle& allocation) {
    if (!initialized() || kind == StaticBufferKind::Count || !allocation) return;
    retired_.push_back({kind, allocation, retirementSerial_});
}

void BufferManager::collect(uint64_t completedSerial) {
    auto out = retired_.begin();
    for (auto it = retired_.begin(); it != retired_.end(); ++it) {
        if (it->serial <= completedSerial) {
            if (StaticBuffer* block = blockOf(it->kind, it->allocation.address))
                block->free(it->allocation);
        } else
            *out++ = *it;
    }
    retired_.erase(out, retired_.end());
}

void BufferManager::resetUpload() {
    upload_.clear();
}
