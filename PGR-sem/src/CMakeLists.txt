cmake_minimum_required(VERSION 4.2)



project(pgr-template)

# pgr-framework: needed by the OpenGL target only. Without it the build is
# Vulkan-only rather than a configure failure.
string(ASCII 27 Esc)
set(RED "${Esc}[31;1m")
set(RST "${Esc}[m")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../framework")
  set(PGR_FRAMEWORK_FOUND TRUE)
  add_subdirectory(
          "${CMAKE_CURRENT_SOURCE_DIR}/../framework"
          "../framework"
  )
else()
  set(PGR_FRAMEWORK_FOUND FALSE)
  message(STATUS
          "${RED}../framework not found${RST} - skipping the ${PROJECT_NAME} "
          "(OpenGL) target. Fetch it with\n"
          "\tgit clone https://github.com/bi-pgr/framework\n"
          "next to the folder containing this project. pgr-vk does not need it.")
endif()


# ---------------------------------------------------------------------------
# Third-party: GLFW (windowing, replaces freeglut) and Dear ImGui (UI,
# replaces AntTweakBar). Both fetched at configure time.
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

# ImGui ships no CMakeLists, so it is declared here and compiled as a static
# library below with the GLFW + OpenGL3 backends.
FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.5
        GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw imgui)

add_library(imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/imgui_demo.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(imgui PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
)
target_link_libraries(imgui PUBLIC glfw)
set_property(TARGET imgui PROPERTY CXX_STANDARD 20)

# The Vulkan target.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
  add_subdirectory(vk)
else()
  message(STATUS "Vulkan SDK not found - skipping the pgr-vk target. "
                 "Install https://vulkan.lunarg.com/ and reconfigure to enable it.")
endif()



# The OpenGL target. Everything below needs the framework: pgr.h, pgr.lib, its
# bundled GLM, and the Assimp/DevIL DLLs.
if(PGR_FRAMEWORK_FOUND)

add_executable(${PROJECT_NAME}
        Main/template-main.cpp
        Camera/Camera.cpp
        Geometry/Mesh/Model.cpp
        Geometry/StaticMesh/StaticMesh.cpp
        Scene/Scene.cpp
        Material/Material.cpp
        Renderer/RenderBuffer/RenderBufferManager.cpp
        Renderer/DeferedRenderer/DeferredRenderer.cpp
        Renderer/ForwardRenderer/ForwardRenderer.cpp
        Renderer/PostProcessRenderer/PostProcessRenderer.cpp
        Window/AppWindow.cpp
        Material/NamedMaterialUniforms.ixx
        PostProcess/FXAA/FXAAPostProcessEffect.ixx
        curves/QuadBeizer.ixx
        Assets/Helicopter/Helicopter.ixx
        Geometry/Basic/Shapes/Sphere.ixx
        ConfigParser/SunConfigParser.ixx
)

target_sources(${PROJECT_NAME} PUBLIC
        FILE_SET CXX_MODULES FILES
        ConfigParser/SunConfigParser.ixx
        Main/PostProcessManager.ixx
        Camera/Camera.ixx
        Geometry/Mesh/Model.ixx
        Geometry/StaticMesh/StaticMesh.ixx
        Geometry/Basic/Vertex/Vertex.ixx
        Geometry/Basic/BoundingBox/BoundingBox.ixx
        Scene/Scene.ixx
        Material/Material.ixx
        LightSource/LightSource.ixx
        LightSource/DirectionalLight.ixx
        LightSource/PointLight.ixx
        LightSource/SpotLight.ixx
        Renderer/IRenderer.ixx
        Renderer/BlitShader/BlitShader.ixx
        Renderer/RenderBuffer/RenderBufferManager.ixx
        Renderer/DeferedRenderer/DeferredRenderer.ixx
        Renderer/ForwardRenderer/ForwardRenderer.ixx
        Renderer/PostProcessRenderer/PostProcessRenderer.ixx
        PostProcess/PostProcessEffect.ixx
        PostProcess/PostProcessShader.ixx
        Logging/Logger.ixx
        PostProcess/ToneMapping/ToneMappingPostProcessEffect.ixx
        PostProcess/SSAO/SSAOPostProcessEffect.ixx
        PostProcess/SSR/SSRPostProcessEffect.ixx
        PostProcess/Fog/FogPostProcessEffect.ixx
        PostProcess/FXAA/FXAAPostProcessEffect.ixx
        Renderer/RenderMode.ixx
        Renderer/ShaderLoader/ShaderLoader.ixx
        Material/NamedMaterialUniforms.ixx
        Window/AppWindow.ixx
        curves/QuadBeizer.ixx
        Assets/Helicopter/Helicopter.ixx
        Assets/UFO/Ufo.ixx
        Geometry/Basic/Shapes/Sphere.ixx
        ConfigParser/ConfigParser.ixx)


set(MODULE_FILES
        ConfigParser/SunConfigParser.ixx
        ConfigParser/ConfigParser.ixx
        Material/NamedMaterialUniforms.ixx
        Main/PostProcessManager.ixx
        Camera/Camera.ixx
        Geometry/Mesh/Model.ixx
        Geometry/StaticMesh/StaticMesh.ixx
        Geometry/Basic/Vertex/Vertex.ixx
        Geometry/Basic/BoundingBox/BoundingBox.ixx
        Scene/Scene.ixx
        Material/Material.ixx
        LightSource/LightSource.ixx
        LightSource/DirectionalLight.ixx
        LightSource/PointLight.ixx
        LightSource/SpotLight.ixx
        Renderer/IRenderer.ixx
        Renderer/BlitShader/BlitShader.ixx
        Renderer/ShaderLoader/ShaderLoader.ixx
        Renderer/RenderBufferManager.ixx
        Renderer/DeferredRenderer.ixx
        Renderer/ForwardRenderer.ixx
        Renderer/PostProcessRenderer.ixx
        PostProcess/PostProcessEffect.ixx
        PostProcess/PostProcessShader.ixx
        Logging/Logger.ixx
        PostProcess/ToneMapping/ToneMappingPostProcessEffect.ixx
        PostProcess/SSAO/SSAOPostProcessEffect.ixx
        PostProcess/SSR/SSRPostProcessEffect.ixx
        PostProcess/Fog/FogPostProcessEffect.ixx
        Rendering.ixx
        Window/Window.ixx
        curves/QuadBeizer.ixx
        Assets/Helicopter/Helicopter.ixx
        Geometry/Basic/Shapes/Sphere.ixx
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wno-register)




set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /wd5033 /MP /Zc:preprocessor)
endif()

target_precompile_headers(${PROJECT_NAME} PRIVATE
        <glm/glm.hpp>
        <glm/gtc/type_ptr.hpp>
        <glm/gtc/matrix_transform.hpp>
        <algorithm>
        <string>
        <iostream>
        <memory>
        <cmath>
        <pgr.h>
)

find_package(OpenGL REQUIRED)
target_link_libraries(${PROJECT_NAME} OpenGL::GL)
target_link_libraries(${PROJECT_NAME} pgr)
target_link_libraries(${PROJECT_NAME} glfw imgui)

# Copy Shaders and Assets next to the executable after each build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Shaders"
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Assets"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Assets"
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Config"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Config"
)




# Copy DLLs next to executable on Windows
if(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # x64: copy only the 64-bit DLLs with their runtime-expected names
    set(FRAMEWORK_DLLS
            "C:/Program Files/Assimp/bin/x64/assimp-vc143-mt.dll"
    )
    # DevIL64.dll must be copied as DevIL.dll (that's the name the import lib references)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../framework/DevIL/bin/DevIL64.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/DevIL.dll"
    )
  else()
    set(FRAMEWORK_DLLS
            "${CMAKE_CURRENT_SOURCE_DIR}/../framework/assimp/bin/assimp.dll"
            "${CMAKE_CURRENT_SOURCE_DIR}/../framework/DevIL/bin/DevIL.dll"
    )
  endif()
  foreach(DLL ${FRAMEWORK_DLLS})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
  endforeach()
endif()

endif() # PGR_FRAMEWORK_FOUND