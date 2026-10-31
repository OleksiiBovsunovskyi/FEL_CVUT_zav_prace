
#Cpp name | Slang name | Size in bytes in the slang | Alignment in bytes in the slang natural layout
SimpleTypeList:dict[str, tuple[str, int, int]] = {
    "uint32_t": ("uint", 4, 4),
    "int32_t": ("int", 4, 4),
    "float": ("float", 4, 4),
    "GpuBool": ("bool", 4, 4),
    "glm::vec2": ("float2", 8, 4),
    "glm::vec3": ("float3", 12, 4),
    "glm::vec4": ("float4", 16, 4),
    "glm::uvec2": ("uint2", 8, 4),
    "glm::uvec3": ("uint3", 12, 4),
    "glm::uvec4": ("uint4", 16, 4),
    "glm::mat4": ("float4x4", 64, 4),
    "int": ("int", 4, 4)
}

#Cpp name | Slang name | Size in bytes in the slang | Alignment in bytes in the slang natural layout
TemplateTypeList:dict[str, tuple[str, int, int]] = {
    "GpuPtr": ("*", 8, 8),
    }

def coreType(t) -> tuple[str, int, int] | None:
    """
    (Slang name, size, alignment) of a core type, from TemplateTypeList for a template, else SimpleTypeList.
    @param t ParsedType to look up
    """
    return (TemplateTypeList if t.is_template else SimpleTypeList).get(t.type)

#Struct name | C++ source of a struct declared in an outside header, generated where it is first used
ExternalStructList:dict[str, str] = {
    "VkDrawMeshTasksIndirectCommandEXT":
        "struct VkDrawMeshTasksIndirectCommandEXT { uint32_t groupCountX; uint32_t groupCountY; uint32_t groupCountZ; };",
}

#Cpp expression | Slang expression, replaced inside constant initializers
ConstantExpressionList:dict[str, str] = {
    "std::numeric_limits<uint32_t>::max()": "0xffffffffu",
}