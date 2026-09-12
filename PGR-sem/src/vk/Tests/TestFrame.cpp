/* Compile-time checks of Frame's public boundary. */

module;

#include <memory>
#include <span>
#include <type_traits>
#include <vulkan/vulkan.hpp>

module Frame;

template <typename T>
concept HasCollectionAcquire = requires(std::span<const std::unique_ptr<T>> frames,
                                        vk::Semaphore imageAvailable) {
    T::acquire(frames, imageAvailable);
};

template <typename T>
concept HasImageIndex = requires { typename T::ImageIndex; };

template <typename T>
concept HasPresentation = requires(T& frame) { frame.present(); };

template <typename T>
concept HasRelease = requires(T& frame) { frame.release(); };

template <typename T>
concept HasMutableTarget = requires(T& frame) { frame.target(); };

static_assert(!HasCollectionAcquire<Frame>);
static_assert(!HasImageIndex<Frame>);
static_assert(!HasPresentation<Frame>);
static_assert(HasRelease<Frame>);
static_assert(!HasMutableTarget<Frame>);
static_assert(!std::is_copy_constructible_v<Frame>);
static_assert(!std::is_move_constructible_v<Frame>);
static_assert(!std::is_default_constructible_v<Frame::Recording>);
static_assert(!std::is_copy_constructible_v<Frame::Recording>);
static_assert(!std::is_move_constructible_v<Frame::Recording>);
