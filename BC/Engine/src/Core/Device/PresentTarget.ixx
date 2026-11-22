module;

#include <cstdint>

export module PresentTarget;

import vulkan;
export import Frame;

/**
 * Where a FrameRunner's finished Frames go.
 * Implemented by ExternalTarget.
 */
export class /*Interface*/ PresentTarget {
public:
    struct Acquisition {
        vk::Result result;
        Frame* frame;
    };

    virtual ~PresentTarget() = default;

    [[nodiscard]] virtual vk::Format   format()        const = 0;
    [[nodiscard]] virtual vk::Extent2D extent()        const = 0;
    [[nodiscard]] virtual uint32_t     imageCount()    const = 0;
    [[nodiscard]] virtual uint32_t     minImageCount() const = 0;

    /// Image layout and access a Frame ends its recording in.
    [[nodiscard]] virtual Frame::ImageUse finalImageUse() const = 0;

    /**
     * Rebuilds the images at the target's current size.
     * Every per-image resource held elsewhere must be rebuilt after this.
     */
    virtual bool recreate() = 0;

    /**
     * @param imageAvailable binary semaphore signaled when the returned Frame may be used.
     * @return Vulkan result and the Frame to record into.
     */
    [[nodiscard]] virtual Acquisition acquire(vk::Semaphore imageAvailable) = 0;

    /**
     * @param frame Frame returned by acquire() and already submitted.
     * @return eSuccess, eSuboptimalKHR or eErrorOutOfDateKHR when recreate() is due.
     */
    [[nodiscard]] virtual vk::Result present(Frame& frame) = 0;
};
