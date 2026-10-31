module;

#include <cstdint>

export module GPUDepthPyramid;
/**
 * Records of the depth pyramid build.
 */

export struct GPUDepthPyramidCounter {
    uint32_t finishedWorkgroups;
};
static_assert(sizeof(GPUDepthPyramidCounter) == 4);
