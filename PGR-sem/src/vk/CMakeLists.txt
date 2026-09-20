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

# VMA: device memory suballocation. Its implementation is compiled inside the
# vk_mem_alloc module.
# VulkanMemoryAllocator-Hpp carries VMA itself as a submodule and adds the
# vma:: bindings over it, so it declares GPUOpen::VulkanMemoryAllocator too.
FetchContent_Declare(vma
        GIT_REPOSITORY https://github.com/YaaZ/VulkanMemoryAllocator-Hpp.git
        GIT_TAG        v3.4.0+2
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES_RECURSE TRUE
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

# stb, for stb_image.h. No release tags and no CMakeLists, so it is pinned to a
# commit and FetchContent only populates it.
FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
)

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(meshoptimizer
        GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
        GIT_TAG        v1.1
        GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(vk-bootstrap vma fastgltf meshoptimizer stb)

# glm 1.0.1 asks for CMake 3.6; CMake 4 deprecates anything below 3.10.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
FetchContent_MakeAvailable(glm)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# ---------------------------------------------------------------------------
# Named-module builds of the STL, Vulkan-Hpp and VMA-Hpp.
#
# Declared after FetchContent_MakeAvailable(vma): VMA-Hpp defines its own module
# target when Vulkan::HppModule already exists, and that target sets
# CXX_MODULE_STD ON, which CMake supports only for libc++ and libstdc++.
#
# std.ixx is the MSVC STL's module source; clang consumes it under
# -fms-compatibility. The two warnings it trips are about its own spelling.
if (DEFINED ENV{VCToolsInstallDir})
    cmake_path(SET VC_TOOLS "$ENV{VCToolsInstallDir}")
else ()
    file(GLOB VC_CANDIDATES "C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*")
    list(SORT VC_CANDIDATES)
    list(POP_BACK VC_CANDIDATES VC_TOOLS)
endif ()
if (NOT EXISTS "${VC_TOOLS}/modules/std.ixx")
    message(FATAL_ERROR "No std.ixx under '${VC_TOOLS}/modules'. "
            "Configure from a Visual Studio developer environment.")
endif ()

# A BMI carries every decl reachable from the interface, so importing vulkan
# put ~40 MB in each one and clang spent ~10x longer serializing a BMI than
# parsing the interface. Reduced BMIs drop what no importer can name.
add_library(module_flags INTERFACE)
target_compile_options(module_flags INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-fmodules-reduced-bmi>)

add_library(std_module STATIC)
target_sources(std_module PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS "${VC_TOOLS}/modules"
        FILES "${VC_TOOLS}/modules/std.ixx")
target_compile_features(std_module PUBLIC cxx_std_23)
target_link_libraries(std_module PRIVATE module_flags)
target_compile_options(std_module PRIVATE
        -Wno-reserved-module-identifier -Wno-include-angled-in-module-purview)

add_library(VulkanHppModule STATIC)
add_library(Vulkan::HppModule ALIAS VulkanHppModule)
target_sources(VulkanHppModule PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS "${Vulkan_INCLUDE_DIR}"
        FILES "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm")
target_compile_features(VulkanHppModule PUBLIC cxx_std_23)
target_link_libraries(VulkanHppModule PRIVATE module_flags)
target_compile_definitions(VulkanHppModule PUBLIC VULKAN_HPP_CXX_MODULE_EXPERIMENTAL_WARNING=)
target_link_libraries(VulkanHppModule PUBLIC Vulkan::Headers std_module)

add_library(VmaHppModule STATIC)
target_sources(VmaHppModule PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS "${vma_SOURCE_DIR}/include"
        FILES "${vma_SOURCE_DIR}/include/vk_mem_alloc.cppm")
target_compile_features(VmaHppModule PUBLIC cxx_std_23)
target_link_libraries(VmaHppModule PRIVATE module_flags)
target_link_libraries(VmaHppModule PUBLIC
        VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
        GPUOpen::VulkanMemoryAllocator
        VulkanHppModule std_module)

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
# stb_image, the PNG and JPEG decoder GltfLoader hands to TextureManager. The
# header carries its own implementation, compiled here away from the module
# flags pgr-vk carries.
# ---------------------------------------------------------------------------
add_library(stb_image STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/stb_image_impl.cpp"
)
target_include_directories(stb_image PUBLIC "${stb_SOURCE_DIR}")
set_property(TARGET stb_image PROPERTY CXX_STANDARD 20)

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
# a Slang module the shaders import. gpu_types.slang is generated next to the
# shaders that import it and must not be edited; slangd resolves an import only
# from the importing file's own directory.
# ---------------------------------------------------------------------------
set(VK_GENERATED_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Core")
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
        "${VK_SHADER_DIR}/Core/mesh_vertex.slang"
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
                  -I "${VK_SHADER_DIR}/Core"
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
add_slang_shader(Core/mesh_frag.slang           fragMain fragment mesh_frag.spv)

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
        VulkanApp.cpp
        VK_Window/AppWindow.cpp
        Core/VulkanContext.cpp
        Core/Swapchain.cpp
        Core/FrameRunner/FrameRunner.cpp
        Rendering/Frame/Frame.cpp
        Core/BufferManager.cpp
        Core/BlockingTransferBatch.cpp
        Core/Pipeline.cpp
        Core/BuildDrawCommands.cpp
        Core/MeshDraw.cpp
        Rendering/Texture/TextureManager.cpp
        VK_Renderer/MeshDrawResources.cpp
        VK_Renderer/ForwardRenderer.cpp
        VK_Renderer/Renderer.cpp
        Debug/ShaderPrint.cpp
        Object/DrawList.cpp
        Object/Component.cpp
        Object/Object.cpp
        # Static-init checks; run once at program start.
        Tests/TestEventsHandling.cpp
        Tests/TestDrawList.cpp
        Tests/TestInput.cpp
        # Compile-time checks; no runtime presence.
        Tests/TestRenderTarget.cpp
        Tests/TestFrame.cpp
        Object/RenderComponent.cpp
        Object/CameraComponent.cpp
        Scene/Scene.cpp
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
        "${CMAKE_CURRENT_SOURCE_DIR}/VulkanApp.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/VK_Window/AppWindow.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/VulkanContext.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Swapchain.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/VkUtil.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/FrameInFlightIndex.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/GPUTypes.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/FrameRunner/FrameRunner.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Buffer/BufferTypes.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Buffer/AllocatedBuffer.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Buffer/SubAllocatedBuffer.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Buffer/StaticBuffers.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/VK_Buffers.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/BufferManager.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/BlockingTransferBatch.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Pipeline.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/BuildDrawCommands.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/MeshDraw.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Debug/ShaderPrint.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/DrawList.ixx"
        # VkScene and its partitions: Component names Object and Scene, Object
        # names Component and Scene, Scene names Object and CameraComponent.
        # Transformable is the base both Object and Component derive from.
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Transformable.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Subscribers.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/EventCapable.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Event.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Component.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Object.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/CameraComponent.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Scene/Scene.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/RenderComponent.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/WASDComponent.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/MouseXYComponent.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Player.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Input/InputHandler.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Material/Material.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Mesh/Mesh.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Mesh/MultiMesh/MultiMesh.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Mesh/MeshLoader/ClusterLODGenerator.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Object/Mesh/MeshLoader/GltfLoader.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ShadersLoader.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Rendering/RenderTarget/RenderTargetBase.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Rendering/RenderTarget/RenderTarget2D.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Rendering/RenderTarget/ColorRenderTarget.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Rendering/RenderTarget/DepthRenderTarget.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Rendering/RenderTarget/RenderTarget.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Rendering/Frame/Frame.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/VK_Renderer/MeshDrawResources.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/VK_Renderer/ForwardRenderer.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/VK_Renderer/Renderer.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Rendering/Image/Image.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/Rendering/Texture/TextureManager.ixx"
        "${PGR_SRC_ROOT}/Logging/Logger.ixx"
)

add_dependencies(pgr-vk pgr-vk-shaders)

target_link_libraries(pgr-vk PRIVATE
        module_flags
        Vulkan::Vulkan
        VulkanHppModule
        VmaHppModule
        std_module
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
        VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
        fastgltf::fastgltf
        glfw
        imgui-vk
        meshoptimizer
        glm::glm
        stb_image
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

# 23 is what vulkan.cppm, std.ixx and std::views::chunk_by require.
set_property(TARGET pgr-vk PROPERTY CXX_STANDARD 23)
set_property(TARGET pgr-vk PROPERTY CXX_STANDARD_REQUIRED ON)

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
