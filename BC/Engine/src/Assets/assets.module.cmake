module(assets
        DEPENDS Logging glm::glm Vulkan::Headers
        PRIVATE_DEPENDS fastgltf::fastgltf meshoptimizer stb_image bc7enc
)
# clusterlod.h ships in meshoptimizer's demo directory.
target_include_directories(assets PRIVATE "${meshoptimizer_SOURCE_DIR}/demo")
# GLM depth range [0, 1].
target_compile_definitions(assets PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
