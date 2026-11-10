module;
#include <cstdint>
#include <glm/glm.hpp>
export module Mesh;

import GPUCoreTypes;
import GPUMaterial;
export import Vertex;
export import Triangle;
export import Meshlet;

/**
 * Mesh header
 * Each meshlet owns a contiguous range of vertices and triangles.
 * Vertex and Meshlet positions are decoded with grid.
 ***/
export struct Mesh {
    GpuPtr<Vertex> vertices{};
    GpuPtr<Meshlet> meshlets{};
    GpuPtr<Triangle> triangles{};
    PositionGrid grid{};
    glm::vec4 boundingSphere{};          // mesh space center in xyz, radius in w
    uint32_t meshletCount = 0;
    uint32_t triangleCount = 0;
    GPUMaterialIndex material{};
};

static_assert(sizeof(Mesh) == 72);
