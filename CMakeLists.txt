cmake_minimum_required(VERSION 4.2)

# A container for two independent renderers. BC/ is the Vulkan one and
# PGR-sem/ the OpenGL one; they share GLFW and the Dear ImGui sources fetched
# here and nothing else.
project(pgr)

# ---------------------------------------------------------------------------
# Third-party: GLFW (windowing) and Dear ImGui (UI). Both fetched at configure
# time and used by both renderers.
# ---------------------------------------------------------------------------
include(FetchContent)

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
)

# ImGui ships no CMakeLists. Each renderer compiles it with its own backend, so
# only the sources are fetched here.
FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.5
        GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw imgui)

# ---------------------------------------------------------------------------
# pgr-vk, the Vulkan renderer. Needs no framework.
# ---------------------------------------------------------------------------
find_package(Vulkan QUIET)
if (Vulkan_FOUND)
  add_subdirectory(BC)
else ()
  message(STATUS "Vulkan SDK not found - skipping the pgr-vk target. "
                 "Install https://vulkan.lunarg.com/ and reconfigure to enable it.")
endif ()

# ---------------------------------------------------------------------------
# pgr-template, the OpenGL renderer. Needs pgr-framework: pgr.h, pgr.lib, its
# bundled GLM and the Assimp/DevIL DLLs.
# ---------------------------------------------------------------------------
string(ASCII 27 Esc)
set(RED "${Esc}[31;1m")
set(RST "${Esc}[m")

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PGR-sem/framework")
  add_subdirectory(PGR-sem)
else ()
  message(STATUS
          "${RED}PGR-sem/framework not found${RST} - skipping the pgr-template "
          "(OpenGL) target. Fetch it with\n"
          "\tgit clone https://github.com/bi-pgr/framework\n"
          "into PGR-sem/. pgr-vk does not need it.")
endif ()
