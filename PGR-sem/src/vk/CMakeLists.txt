# ===========================================================================
# pgr-vk — Vulkan target.
# ===========================================================================

# shaderc_combined ships with the SDK: glslang + SPIRV-Tools in one static lib.
# It is what ShaderLoader uses to compile GLSL (and resolve #include) at runtime.
find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)

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
# GLSL -> SPIR-V at build time. This is now an ahead-of-time check rather than
# the only path: ShaderLoader compiles the same sources at runtime, so a broken
# shader is caught here at build time and reported again (with the include
# stack) at load time. The sources themselves are copied next to the executable
# further down.
# ---------------------------------------------------------------------------
find_program(GLSL_COMPILER
        NAMES glslangValidator glslc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
)
if(NOT GLSL_COMPILER)
  message(FATAL_ERROR
          "No glslangValidator/glslc found. Install the Vulkan SDK "
          "(https://vulkan.lunarg.com/) and make sure VULKAN_SDK is set.")
endif()
message(STATUS "Shader compiler: ${GLSL_COMPILER}")

set(VK_SHADER_SOURCES
        shaders/triangle.vert
        shaders/triangle.frag
        shaders/mesh.mesh
        shaders/mesh.frag
        shaders/Core/build_draw_commands.comp
)

set(VK_SPV_FILES "")
foreach(SHADER ${VK_SHADER_SOURCES})
  get_filename_component(SHADER_NAME "${SHADER}" NAME)
  set(SPV_OUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/${SHADER_NAME}.spv")
  add_custom_command(
          OUTPUT  "${SPV_OUT}"
          COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders"
          # -I mirrors ShaderLoader::addIncludeDir so both compilers resolve
          # #include the same way.
          COMMAND "${GLSL_COMPILER}" --target-env vulkan1.3
                  "-I${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                  "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}" -o "${SPV_OUT}"
          DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}"
          COMMENT "SPIR-V: ${SHADER_NAME}"
          VERBATIM
  )
  list(APPEND VK_SPV_FILES "${SPV_OUT}")
endforeach()

add_custom_target(pgr-vk-shaders DEPENDS ${VK_SPV_FILES})

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
        Debug/ShaderPrint.cpp
        Object/Material/Material.cpp
        Object/Mesh/Mesh.cpp
        Object/Mesh/MeshLoader/ClusterLODGenerator.cpp
        Object/Mesh/MeshLoader/GltfLoader.cpp
        shaders/ShadersLoader.cpp
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
        Vulkan::shaderc_combined
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

# ...and the GLSL sources, which ShaderLoader compiles at runtime. Only the
# shader files, not ShadersLoader.* which shares this directory. CONFIGURE_DEPENDS
# re-globs on build, so a newly added .glsl does not need a manual reconfigure.
file(GLOB VK_SHADER_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.geom"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.tesc"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.tese"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.task"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.mesh"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl"
)
add_custom_command(TARGET pgr-vk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pgr-vk>/Shaders/src"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${VK_SHADER_FILES} "$<TARGET_FILE_DIR:pgr-vk>/Shaders/src"
)

# Preserve the nested path expected by BuildDrawCommands' runtime shader load.
add_custom_command(TARGET pgr-vk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:pgr-vk>/Shaders/src/Core"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Core/build_draw_commands.comp"
                "$<TARGET_FILE_DIR:pgr-vk>/Shaders/src/Core/build_draw_commands.comp"
)
