cmake_minimum_required(VERSION 4.2)

# Container for both VK and GL renderers
project(pgr)

#Thirdparty
include(FetchContent)

#GLFW
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
)

# ImGui 
FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.5
        GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw imgui)

#VK
find_package(Vulkan QUIET)
if (Vulkan_FOUND)
  add_subdirectory(BC)
else ()
  message(STATUS "Vulkan SDK not found - skipping the pgr-vk target. "
                 "Install https://vulkan.lunarg.com/ and reconfigure to enable it.")
endif ()

# ---------------------------------------------------------------------------
# pgr-template, (GL). Needs pgr-framework: pgr.h, pgr.lib, its
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
