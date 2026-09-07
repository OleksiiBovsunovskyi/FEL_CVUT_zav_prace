/* Compile-time checks of render target extent, mip chain and view type rules. */

module;

#include <vulkan/vulkan.hpp>

module renderTarget;

static_assert(!extentFitsType(vk::ImageType::e2D, {1920, 1080, 0}),
              "a zero in any dimension is not a usable extent");
static_assert(!extentFitsType(vk::ImageType::e2D, {1920, 1080, 2}),
              "a 2D image has depth 1");
static_assert(!extentFitsType(vk::ImageType::e1D, {1920, 2, 1}),
              "a 1D image has height 1");
static_assert(extentFitsType(vk::ImageType::e3D, {64, 64, 64}));

static_assert(maxMipLevels({1, 1, 1}) == 1,
              "a 1x1 image is already its own last level");
static_assert(maxMipLevels({1920, 1080, 1}) == 11,
              "the chain halves the longest side, 1920 down to 1");

static_assert(viewTypeFor(vk::ImageType::e2D, 1) == vk::ImageViewType::e2D);
static_assert(viewTypeFor(vk::ImageType::e2D, 4) == vk::ImageViewType::e2DArray,
              "more than one layer needs an array view");
static_assert(viewTypeFor(vk::ImageType::e3D, 1) == vk::ImageViewType::e3D);
