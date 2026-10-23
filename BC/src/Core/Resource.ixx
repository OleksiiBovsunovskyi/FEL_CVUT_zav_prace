module;
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

export module Resource;

import vulkan;


export struct Use;
/**
 * Pipeline stages, accesses and layout of one use of a resource.
 * layout is eUndefined for buffers.
 */
export struct ResourceAccess {
    vk::PipelineStageFlags2 stageMask  = vk::PipelineStageFlagBits2::eNone;
    vk::AccessFlags2        accessMask = vk::AccessFlagBits2::eNone;
    vk::ImageLayout         layout     = vk::ImageLayout::eUndefined;

    /** @return false until the resource is used for the first time. */
    [[nodiscard]] bool defined() const { return stageMask != vk::PipelineStageFlagBits2::eNone; }

    /** @return true when accessMask holds read bits only. */
    [[nodiscard]] bool readOnly() const {
        constexpr vk::AccessFlags2 reads =
            vk::AccessFlagBits2::eIndirectCommandRead |
            vk::AccessFlagBits2::eIndexRead |
            vk::AccessFlagBits2::eVertexAttributeRead |
            vk::AccessFlagBits2::eUniformRead |
            vk::AccessFlagBits2::eInputAttachmentRead |
            vk::AccessFlagBits2::eShaderRead |
            vk::AccessFlagBits2::eShaderStorageRead |
            vk::AccessFlagBits2::eShaderSampledRead |
            vk::AccessFlagBits2::eColorAttachmentRead |
            vk::AccessFlagBits2::eDepthStencilAttachmentRead |
            vk::AccessFlagBits2::eTransferRead |
            vk::AccessFlagBits2::eHostRead |
            vk::AccessFlagBits2::eMemoryRead;
        return !(accessMask & ~reads);
    }
};

/**
 * Image and buffer barriers collected for one vkCmdPipelineBarrier2.
 */
export class BarrierList {
public:
    void add(const vk::ImageMemoryBarrier2& b)  { images_.push_back(b); }
    void add(const vk::BufferMemoryBarrier2& b) { buffers_.push_back(b); }

    /**
     * Records every collected barrier in one pipelineBarrier2;
     * @param cmd command buffer in the recording state
     */
    void record(vk::CommandBuffer cmd) const {
        if (images_.empty() && buffers_.empty()) return;

        vk::DependencyInfo dep{};
        dep.imageMemoryBarrierCount  = static_cast<uint32_t>(images_.size());
        dep.pImageMemoryBarriers     = images_.data();
        dep.bufferMemoryBarrierCount = static_cast<uint32_t>(buffers_.size());
        dep.pBufferMemoryBarriers    = buffers_.data();
        cmd.pipelineBarrier2(dep);
    }

private:
    std::vector<vk::ImageMemoryBarrier2>  images_;
    std::vector<vk::BufferMemoryBarrier2> buffers_;
};


/**
 * GPU resource that tracks its last access and emits the barrier to the next one.
 * State is valid while commands are recorded in submission order on a single queue.
 */
export class IResource {
public:
    virtual ~IResource() = default;

    /**
     * Moves the resource to access and records the barrier, if one is needed.
     * @param cmd command buffer in the recording state
     * @param access use of the resource by the next recorded command
     */
    void use(vk::CommandBuffer cmd, const ResourceAccess& access) {
        BarrierList list;
        collect(access, list);
        list.record(cmd);
    }


protected:
    /**
     * Moves the resource to access and adds the barrier, if one is needed, to list.
     * @param targetAccess use of the resource by the next recorded command
     * @param list barriers to be recorded before that command
    */
    virtual void collect(const ResourceAccess& targetAccess, BarrierList& list) = 0;
    //Static helper to batch use for multiple resources.
    friend void use(vk::CommandBuffer cmd, std::initializer_list<Use> uses);

    /**
     * Replaces access_ with requestedAccess and returns the access a barrier to it has to wait on.
     * @param requestedAccess use of the resource we need
     * @return access_ before the call; empty when no barrier is needed
     */
    std::optional<ResourceAccess> exchangeAccess(const ResourceAccess& requestedAccess) {
        const ResourceAccess previous      = access_;
        const bool           layoutChange  = previous.layout != requestedAccess.layout;
        const bool           readAfterRead = !layoutChange && previous.readOnly() && requestedAccess.readOnly();
        
        if (readAfterRead) {//We should be conservative, so we will cover barriers skipped with 
            //Read after read rule when they will finally be needed.
            access_.stageMask  |= requestedAccess.stageMask;
            access_.accessMask |= requestedAccess.accessMask;
        } else {
            access_ = requestedAccess;
        }

        if (layoutChange) return previous;
        if (!previous.defined()) return std::nullopt;
        //It is read after read and no new stage was introduced, so no barrier needed
        if (readAfterRead && (previous.stageMask & requestedAccess.stageMask) == requestedAccess.stageMask) return std::nullopt;
        return previous;
    }

    //What do we need to wait for before this resource is used (access we are transitioning from)
    ResourceAccess access_{};
};

/** One resource and its requested access, for use(cmd, {...}). */
export struct Use {
    IResource&     resource;
    ResourceAccess access;
};

/**
 * Moves every resource to its access and records all needed barriers in one pipelineBarrier2.
 * @param cmd command buffer in the recording state
 * @param uses resources used by the next recorded command
 */
export inline void use(vk::CommandBuffer cmd, std::initializer_list<Use> uses) {
    BarrierList list;
    for (const Use& u : uses) u.resource.collect(u.access, list);
    list.record(cmd);
}
