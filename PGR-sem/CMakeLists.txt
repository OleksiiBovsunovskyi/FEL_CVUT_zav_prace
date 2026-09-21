project(pgr-template)

add_subdirectory(framework)

# Dear ImGui with the GLFW + OpenGL3 backends. pgr-vk builds its own from the
# same fetched sources with the Vulkan backend.
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

# The OpenGL target. Everything below needs the framework: pgr.h, pgr.lib, its
# bundled GLM, and the Assimp/DevIL DLLs.

add_executable(${PROJECT_NAME}
        src/Main/template-main.cpp
        src/Camera/Camera.cpp
        src/Geometry/Mesh/Model.cpp
        src/Geometry/StaticMesh/StaticMesh.cpp
        src/Scene/Scene.cpp
        src/Material/Material.cpp
        src/Renderer/RenderBuffer/RenderBufferManager.cpp
        src/Renderer/DeferedRenderer/DeferredRenderer.cpp
        src/Renderer/ForwardRenderer/ForwardRenderer.cpp
        src/Renderer/PostProcessRenderer/PostProcessRenderer.cpp
        src/Window/AppWindow.cpp
        src/Material/NamedMaterialUniforms.ixx
        src/PostProcess/FXAA/FXAAPostProcessEffect.ixx
        src/curves/QuadBeizer.ixx
        src/Assets/Helicopter/Helicopter.ixx
        src/Geometry/Basic/Shapes/Sphere.ixx
        src/ConfigParser/SunConfigParser.ixx
)

target_sources(${PROJECT_NAME} PUBLIC
        FILE_SET CXX_MODULES FILES
        src/ConfigParser/SunConfigParser.ixx
        src/Main/PostProcessManager.ixx
        src/Camera/Camera.ixx
        src/Geometry/Mesh/Model.ixx
        src/Geometry/StaticMesh/StaticMesh.ixx
        src/Geometry/Basic/Vertex/Vertex.ixx
        src/Geometry/Basic/BoundingBox/BoundingBox.ixx
        src/Scene/Scene.ixx
        src/Material/Material.ixx
        src/LightSource/LightSource.ixx
        src/LightSource/DirectionalLight.ixx
        src/LightSource/PointLight.ixx
        src/LightSource/SpotLight.ixx
        src/Renderer/IRenderer.ixx
        src/Renderer/BlitShader/BlitShader.ixx
        src/Renderer/RenderBuffer/RenderBufferManager.ixx
        src/Renderer/DeferedRenderer/DeferredRenderer.ixx
        src/Renderer/ForwardRenderer/ForwardRenderer.ixx
        src/Renderer/PostProcessRenderer/PostProcessRenderer.ixx
        src/PostProcess/PostProcessEffect.ixx
        src/PostProcess/PostProcessShader.ixx
        src/Logging/Logger.ixx
        src/PostProcess/ToneMapping/ToneMappingPostProcessEffect.ixx
        src/PostProcess/SSAO/SSAOPostProcessEffect.ixx
        src/PostProcess/SSR/SSRPostProcessEffect.ixx
        src/PostProcess/Fog/FogPostProcessEffect.ixx
        src/PostProcess/FXAA/FXAAPostProcessEffect.ixx
        src/Renderer/RenderMode.ixx
        src/Renderer/ShaderLoader/ShaderLoader.ixx
        src/Material/NamedMaterialUniforms.ixx
        src/Window/AppWindow.ixx
        src/curves/QuadBeizer.ixx
        src/Assets/Helicopter/Helicopter.ixx
        src/Assets/UFO/Ufo.ixx
        src/Geometry/Basic/Shapes/Sphere.ixx
        src/ConfigParser/ConfigParser.ixx)


set(MODULE_FILES
        src/ConfigParser/SunConfigParser.ixx
        src/ConfigParser/ConfigParser.ixx
        src/Material/NamedMaterialUniforms.ixx
        src/Main/PostProcessManager.ixx
        src/Camera/Camera.ixx
        src/Geometry/Mesh/Model.ixx
        src/Geometry/StaticMesh/StaticMesh.ixx
        src/Geometry/Basic/Vertex/Vertex.ixx
        src/Geometry/Basic/BoundingBox/BoundingBox.ixx
        src/Scene/Scene.ixx
        src/Material/Material.ixx
        src/LightSource/LightSource.ixx
        src/LightSource/DirectionalLight.ixx
        src/LightSource/PointLight.ixx
        src/LightSource/SpotLight.ixx
        src/Renderer/IRenderer.ixx
        src/Renderer/BlitShader/BlitShader.ixx
        src/Renderer/ShaderLoader/ShaderLoader.ixx
        src/Renderer/RenderBufferManager.ixx
        src/Renderer/DeferredRenderer.ixx
        src/Renderer/ForwardRenderer.ixx
        src/Renderer/PostProcessRenderer.ixx
        src/PostProcess/PostProcessEffect.ixx
        src/PostProcess/PostProcessShader.ixx
        src/Logging/Logger.ixx
        src/PostProcess/ToneMapping/ToneMappingPostProcessEffect.ixx
        src/PostProcess/SSAO/SSAOPostProcessEffect.ixx
        src/PostProcess/SSR/SSRPostProcessEffect.ixx
        src/PostProcess/Fog/FogPostProcessEffect.ixx
        src/Rendering.ixx
        src/Window/Window.ixx
        src/curves/QuadBeizer.ixx
        src/Assets/Helicopter/Helicopter.ixx
        src/Geometry/Basic/Shapes/Sphere.ixx
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wno-register)




set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
set_property(TARGET ${PROJECT_NAME} PROPERTY
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${PROJECT_NAME}")

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
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Shaders"
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Assets"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Assets"
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Config"
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
            "${CMAKE_CURRENT_SOURCE_DIR}/framework/DevIL/bin/DevIL64.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/DevIL.dll"
    )
  else()
    set(FRAMEWORK_DLLS
            "${CMAKE_CURRENT_SOURCE_DIR}/framework/assimp/bin/assimp.dll"
            "${CMAKE_CURRENT_SOURCE_DIR}/framework/DevIL/bin/DevIL.dll"
    )
  endif()
  foreach(DLL ${FRAMEWORK_DLLS})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
  endforeach()
endif()
