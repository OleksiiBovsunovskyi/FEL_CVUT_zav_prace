# Rendering: renderers, draw list, textures, render target descriptions.
module(Rendering
        DEPENDS Core Debug
)
# static_asserts of GPUTypes offsets against the compiled shaders.
target_sources(Rendering PRIVATE "${VK_LAYOUT_CHECK}")
add_dependencies(Rendering engine-shaders)
