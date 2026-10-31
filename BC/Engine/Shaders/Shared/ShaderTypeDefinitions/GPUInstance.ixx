module;

#include <cstdint>

#include <glm/glm.hpp>

export module GPUInstance;
import GPUMesh;
/**
 * Drawable instance records and their device-side updates.
 */

/**
 * One drawable instance. transform is model-to-world; MultiMesh parts are
 * flattened on the CPU, so one part is one of these.
 *
 * The trailing 8 bytes are alignment padding held for a per-instance material
 * override, which fits without growing the record.
 */
export struct alignas(16) GPUMeshInstance {
    glm::mat4      transform{1.0f};
    MeshDataPointer mesh{};
    glm::uvec2     _padding{};
};

static_assert(sizeof(GPUMeshInstance) == 80);

/**
 * One instance write applied by scatter_instances.slang:
 * instances[index] = instance.
 */
export struct alignas(16) GPUMeshInstanceUpdate {
    uint32_t        index = 0;
    glm::uvec3      _padding{};
    GPUMeshInstance instance{};
};

static_assert(sizeof(GPUMeshInstanceUpdate) == 96);
