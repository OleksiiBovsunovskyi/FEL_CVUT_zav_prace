module;

#include <cstdint>

#include <glm/glm.hpp>

export module GPUFrame;
import GPUCoreTypes;
import GPUDraw;
/**
 * Per-recording camera and frame records.
 */

/**
 * Camera state for one recording
 */
export struct alignas(16) GPUCameraData {
    glm::mat4 viewProj{1.0f};
    //XYZ. W is unused
    glm::vec4 position{0.0f};
};

static_assert(sizeof(GPUCameraData) == 80);

/**
 * Per frame data
 */
export struct alignas(16) GPUFrameData {
    GPUCameraData           camera{};
    GpuPtr<GPUDrawCounters> drawCounters{};
    GpuBool                 debugShowMeshlets;
    GpuBool                 debugShowMeshletSpheres;
    GpuBool                 debugDrawNormals;
    GpuBool                 useHiZOC;
    glm::mat4               hiZViewProj;
    uint32_t                padding[2];
};

static_assert(sizeof(GPUFrameData) == 176);
