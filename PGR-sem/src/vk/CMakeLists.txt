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

FetchContent_MakeAvailable(vk-bootstrap vma)

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
# GLSL -> SPIR-V. Shaders are compiled at build time and dropped next to the
# executable; nothing is compiled at runtime.
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
)

set(VK_SPV_FILES "")
foreach(SHADER ${VK_SHADER_SOURCES})
  get_filename_component(SHADER_NAME "${SHADER}" NAME)
  set(SPV_OUT "${CMAKE_CURRENT_BINARY_DIR}/Shaders/${SHADER_NAME}.spv")
  add_custom_command(
          OUTPUT  "${SPV_OUT}"
          COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Shaders"
          COMMAND "${GLSL_COMPILER}" --target-env vulkan1.3
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
        "${PGR_SRC_ROOT}/Logging/Logger.ixx"
)

add_dependencies(pgr-vk pgr-vk-shaders)

target_link_libraries(pgr-vk PRIVATE
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
        glfw
        imgui-vk
)

target_compile_definitions(pgr-vk PRIVATE
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        # Keep GLFW from including any GL headers.
        GLFW_INCLUDE_NONE
)

target_include_directories(pgr-vk PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../framework/inc"   # glm only
)

set_property(TARGET pgr-vk PROPERTY CXX_STANDARD 20)

# Copy compiled shaders next to the executable.
add_custom_command(TARGET pgr-vk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/Shaders"
        "$<TARGET_FILE_DIR:pgr-vk>/Shaders"
)
