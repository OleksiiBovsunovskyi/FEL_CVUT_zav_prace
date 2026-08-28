# ===========================================================================
# pgr-vk — Vulkan target.
# ===========================================================================

find_package(Vulkan REQUIRED)

include(FetchContent)

# vk-bootstrap: instance/physical-device/device/swapchain creation only. It is
# not present at runtime, so it can be removed later without touching render code.
#
# IMPORTANT: the tag must match the installed Vulkan SDK's header version.
# vk-bootstrap generates its dispatch table from vk.xml, so a newer checkout
# references PFN_* typedefs that older headers do not declare (and vice versa).
# SDK 1.4.350.0 -> v1.4.350. Bump both together.
FetchContent_Declare(vk-bootstrap
        GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
        GIT_TAG        v1.4.350
        GIT_SHALLOW    TRUE
)

# VMA: device memory suballocation. Header-only; compiled in vma_impl.cpp.
FetchContent_Declare(vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG        v3.1.0
        GIT_SHALLOW    TRUE
)

# fastgltf: glTF 2.0 / GLB scene importer. Keep its optional targets out of the
# build; pgr-vk consumes the regular C++ library through fastgltf::fastgltf.
set(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_CPP_MODULES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(fastgltf
        GIT_REPOSITORY https://github.com/spnda/fastgltf.git
        GIT_TAG        v0.9.0
        GIT_SHALLOW    TRUE
)

# GLM. Not framework/inc: that copy is 0.9.3.1, which predates both
# GLM_FORCE_DEPTH_ZERO_TO_ONE and GLM_FORCE_RADIANS. The OpenGL target still
# uses it.
FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
)

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(meshoptimizer
        GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
        GIT_TAG        v1.1
        GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(vk-bootstrap vma fastgltf meshoptimizer glm)

# ---------------------------------------------------------------------------
# Dear ImGui, Vulkan renderer backend. The `imgui` target from the parent
# CMakeLists already carries the core sources and the GLFW platform backend
# (which is API-agnostic); only the renderer backend differs, so it is added
# here instead of being forced on the OpenGL target.
# ---------------------------------------------------------------------------
add_library(imgui-vk STATIC
        "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)
target_link_libraries(imgui-vk PUBLIC imgui Vulkan::Vulkan)
set_property(TARGET imgui-vk PROPERTY CXX_STANDARD 20)

# ---------------------------------------------------------------------------
# Slang -> SPIR-V at build time. The renderer loads only the .spv, so a broken
# shader fails the build rather than the run.
# ---------------------------------------------------------------------------
find_program(SLANG_COMPILER
        NAMES slangc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
)
if(NOT SLANG_COMPILER)
  message(FATAL_ERROR
          "No slangc found. Install the Vulkan SDK "
          "(https://vulkan.lunarg.com/) and make sure VULKAN_SDK is set.")
endif()
message(STATUS "Shader compiler: ${SLANG_COMPILER}")

set(VK_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

# ---------------------------------------------------------------------------
# The GPU records are declared once, in Core/GPUTypes.ixx, and translated into
# a Slang module the shaders import. Generating rather than mirroring them is
# why gpu_types.slang lives in the build tree and must not be edited.
# ---------------------------------------------------------------------------
set(VK_GENERATED_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(VK_GPU_TYPES_SLANG "${VK_GENERATED_SHADER_DIR}/gpu_types.slang")

add_custom_command(
        OUTPUT  "${VK_GPU_TYPES_SLANG}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VK_GENERATED_SHADER_DIR}"
        COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/Core/GPUTypes.ixx"
                "-DOUTPUT=${VK_GPU_TYPES_SLANG}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_slang_structs.cmake"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Core/GPUTypes.ixx"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_slang_structs.cmake"
        COMMENT "Generating gpu_types.slang from GPUTypes.ixx"
        VERBATIM
)

# Imported by the entry points; every shader is rebuilt when one of them changes.
set(VK_SLANG_MODULES
        "${VK_GPU_TYPES_SLANG}"
        "${VK_SHADER_DIR}/Utils/ShereVisibilityTest.slang"
)

set(VK_SPV_FILES "")

# One SPIR-V module per entry point; slangc names the entry point "main" in all
# of them, so the pipelines need no entry name of their own.
function(add_slang_shader SOURCE ENTRY STAGE OUTPUT_NAME)
  set(SPV_OUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/${OUTPUT_NAME}")
  add_custom_command(
          OUTPUT  "${SPV_OUT}"
          COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders"
          COMMAND "${SLANG_COMPILER}" -target spirv -profile spirv_1_6
                  -emit-spirv-directly
                  -I "${VK_SHADER_DIR}" -I "${VK_SHADER_DIR}/Utils"
                  -I "${VK_GENERATED_SHADER_DIR}"
                  "${VK_SHADER_DIR}/${SOURCE}"
                  -entry ${ENTRY} -stage ${STAGE} -o "${SPV_OUT}"
          DEPENDS "${VK_SHADER_DIR}/${SOURCE}" ${VK_SLANG_MODULES}
          COMMENT "Slang: ${OUTPUT_NAME}"
          VERBATIM
  )
  set(VK_SPV_FILES ${VK_SPV_FILES} "${SPV_OUT}" PARENT_SCOPE)
endfunction()

add_slang_shader(Core/build_draw_commands.slang main     compute  build_draw_commands.spv)
add_slang_shader(Core/mesh.slang                meshMain mesh     mesh.spv)
add_slang_shader(Core/mesh.slang                fragMain fragment mesh_frag.spv)

add_custom_target(pgr-vk-shaders DEPENDS ${VK_SPV_FILES})

# ---------------------------------------------------------------------------
# Record layouts are declared twice - Core/GPUTypes.ixx and gpu_types.slang.
# Rather than generating one from the other, the offsets slangc chose are read
# back out of the SPIR-V and turned into static_asserts, so drift is a compile
# error with the shader compiler's own numbers.
# ---------------------------------------------------------------------------
find_program(SPIRV_DIS NAMES spirv-dis
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT SPIRV_DIS)
  message(FATAL_ERROR "No spirv-dis found; it ships with the Vulkan SDK.")
endif()

set(VK_SPVASM_DIR "${CMAKE_CURRENT_BINARY_DIR}/Shaders/asm")
set(VK_SPVASM_FILES "")
foreach(SPV IN LISTS VK_SPV_FILES)
  get_filename_component(SPV_NAME "${SPV}" NAME_WE)
  set(ASM_OUT "${VK_SPVASM_DIR}/${SPV_NAME}.spvasm")
  add_custom_command(
          OUTPUT  "${ASM_OUT}"
          COMMAND ${CMAKE_COMMAND} -E make_directory "${VK_SPVASM_DIR}"
          COMMAND "${SPIRV_DIS}" "${SPV}" -o "${ASM_OUT}"
          DEPENDS "${SPV}"
          COMMENT "Disassembling ${SPV_NAME}.spv"
          VERBATIM
  )
  list(APPEND VK_SPVASM_FILES "${ASM_OUT}")
endforeach()

set(VK_LAYOUT_CHECK "${CMAKE_CURRENT_BINARY_DIR}/generated/gpu_layout_check.cpp")
add_custom_command(
        OUTPUT  "${VK_LAYOUT_CHECK}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND ${CMAKE_COMMAND}
                "-DSPVASM_DIR=${VK_SPVASM_DIR}"
                "-DOUTPUT=${VK_LAYOUT_CHECK}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_layout_check.cmake"
        DEPENDS ${VK_SPVASM_FILES}
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_layout_check.cmake"
        COMMENT "Generating GPU record layout checks"
        VERBATIM
)

# ---------------------------------------------------------------------------
#Executable
# ---------------------------------------------------------------------------
add_executable(pgr-vk
        main.cpp
        vma_impl.cpp
        VK_Window/AppWindow.cpp
        Core/VulkanContext.cpp
        Core/Swapchain.cpp
        Core/FrameRunner.cpp
        Core/VK_Buffers.cpp
        Core/UploadBatch.cpp
        Core/Pipeline.cpp
        Core/BuildDrawCommands.cpp
        Core/MeshDraw.cpp
        Debug/ShaderPrint.cpp
        Object/Material/Material.cpp
        Object/Mesh/Mesh.cpp
        Object/Mesh/MeshLoader/ClusterLODGenerator.cpp
        Object/Mesh/MeshLoader/GltfLoader.cpp
        shaders/ShadersLoader.cpp
        "${VK_LAYOUT_CHECK}"
)

# Logger.ixx is shared verbatim with the OpenGL target; it only uses the std
# library, so it is compiled into this target as well rather than being linked.
# A file set takes a single, non-nested base directory, hence src/ rather than
# src/vk/ - both module sources live under it.
get_filename_component(PGR_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

target_sources(pgr-vk PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS "${PGR_SRC_ROOT}"
        FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/VK_Window/AppWindow.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/VulkanContext.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Swapchain.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/VkUtil.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/GPUTypes.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/FrameRunner.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/VK_Buffers.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/UploadBatch.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Pipeline.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/BuildDrawCommands.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/MeshDraw.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Debug/ShaderPrint.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Object.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Material/Material.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Mesh/Mesh.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Mesh/MultiMesh/MultiMesh.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Mesh/MeshLoader/ClusterLODGenerator.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Mesh/MeshLoader/GltfLoader.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ShadersLoader.ixx"
        "${PGR_SRC_ROOT}/Logging/Logger.ixx"
)

add_dependencies(pgr-vk pgr-vk-shaders)

target_link_libraries(pgr-vk PRIVATE
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
        fastgltf::fastgltf
        glfw
        imgui-vk
        meshoptimizer
        glm::glm
)

target_include_directories(pgr-vk PRIVATE
        "${meshoptimizer_SOURCE_DIR}/demo"
)

target_compile_definitions(pgr-vk PRIVATE
        # Vulkan clip-space depth is [0,1], not OpenGL's [-1,1].
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        # Keep GLFW from including any GL headers.
        GLFW_INCLUDE_NONE
)

set_property(TARGET pgr-vk PROPERTY CXX_STANDARD 20)

# Per-buffer data is keyed off StaticBufferKind/FrameBufferKind by switches with
# no default; a missing case must fail the build, not warn.
target_compile_options(pgr-vk PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-Werror=switch>
        $<$<CXX_COMPILER_ID:MSVC>:/we4062>
)

# Copy compiled shaders next to the executable.
add_custom_command(TARGET pgr-vk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/Shaders"
        "$<TARGET_FILE_DIR:pgr-vk>/Shaders"
)

# The .slang sources are not needed at runtime: only the .spv above is loaded.
