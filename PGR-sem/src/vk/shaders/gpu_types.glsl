#ifndef GPU_TYPES_GLSL
#define GPU_TYPES_GLSL

/**
 * Mirror of Core/GPUTypes.ixx, read through a raw GPU pointer. A field that
 * differs between the two produces garbage. Sizes below are the ones
 * static_assert'd on the C++ side.
 *
 * `scalar` layout is required; std140/std430 pad differently from C++.
 *
 * Including shader must first enable GL_EXT_buffer_reference,
 * GL_EXT_buffer_reference2, GL_EXT_scalar_block_layout and
 * GL_EXT_shader_explicit_arithmetic_types_int64.
 */

struct GPUVertex {          // 64 bytes
    vec4 position;          // w reserved
    vec4 normal;            // w reserved
    vec4 tangent;           // w = handedness
    vec2 texCoord;
    vec2 _padding;
};

struct GPUMeshlet {         // 48 bytes
    /* Element offsets relative to the owning GPUMesh. */
    uint vertexOffset;
    uint triangleOffset;
    uint vertexCount;
    uint triangleCount;
    vec4 boundingSphere;
    vec4 normalCone;        // xyz = axis, w = cos cutoff
};

struct GPUCluster {         // 16 bytes
    uint meshletIndex;
    int  group;
    int  refinedGroup;
    uint _padding;
};

struct GPUClusterGroup {    // 48 bytes
    uint firstCluster;
    uint clusterCount;
    uint depth;
    uint _padding0;
    vec4 boundingSphere;
    float error;
    float _padding1[3];
};

struct GPUMesh {            // 64 bytes
    /* Every one is an element index into the matching mega-buffer. */
    uint vertexOffset;
    uint vertexCount;
    uint meshletOffset;
    uint meshletCount;
    uint meshletVertexIndexOffset;
    uint meshletTriangleOffset;
    uint materialIndex;
    uint _padding;
    uint clusterOffset;
    uint clusterCount;
    uint clusterGroupOffset;
    uint clusterGroupCount;
    vec4 boundingSphere;
};

struct GPUMaterial {        // 80 bytes
    vec4  albedo;
    vec4  emissive;         // rgb = colour, a = intensity
    vec4  surface;          // x=specular y=shininess z=metallic w=roughness
    uvec4 textures;         // bindless indices, 0xffffffff = none
    float alphaThreshold;
    uint  flags;
    uint  _padding[2];
};

#define MATERIAL_EMISSIVE    1u
#define MATERIAL_ALPHA_BLEND 2u
#define MATERIAL_ALPHA_MASK  4u

#define INVALID_TEXTURE_INDEX 0xffffffffu

/* Mega-buffer pointers. */

layout(buffer_reference, scalar, buffer_reference_align = 16) readonly buffer VertexBuffer {
    GPUVertex v[];
};
layout(buffer_reference, scalar, buffer_reference_align = 16) readonly buffer MeshletBuffer {
    GPUMeshlet m[];
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer UintBuffer {
    uint i[];
};
layout(buffer_reference, scalar, buffer_reference_align = 16) readonly buffer MeshBuffer {
    GPUMesh m[];
};
layout(buffer_reference, scalar, buffer_reference_align = 16) readonly buffer MaterialBuffer {
    GPUMaterial m[];
};

/**
 * Arguments to one draw. 116 of the 128 guaranteed push constant bytes; more
 * belongs in the FrameConstants buffer.
 */
layout(push_constant, scalar) uniform DrawPush {
    mat4 viewProj;                  // model already folded in

    VertexBuffer   vertices;        // 64
    MeshletBuffer  meshlets;        // 72
    UintBuffer     meshletVertexIndices;  // 80
    UintBuffer     meshletTriangles;      // 88
    MeshBuffer     meshes;          // 96
    MaterialBuffer materials;       // 104

    uint meshIndex;                 // 112
} pc;                               // 116

#endif // GPU_TYPES_GLSL
