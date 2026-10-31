#project we actually run, uses engine as dependency
cmake_minimum_required(VERSION 3.28)

project(app LANGUAGES C CXX)

if (NOT TARGET engine)
  add_subdirectory(../Engine "${CMAKE_CURRENT_BINARY_DIR}/Engine")
endif ()

add_executable(app)
set_property(TARGET app PROPERTY
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# One module per .module.toml under src/.
add_modules(APP_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/src")
engine_shader_root("${CMAKE_CURRENT_SOURCE_DIR}/Shaders" APP_SHADER_TYPE_MODULES)
target_link_libraries(app PRIVATE ${APP_SHADER_TYPE_MODULES} ${APP_MODULES})
set_property(TARGET app PROPERTY CXX_STANDARD 23)
set_property(TARGET app PROPERTY CXX_STANDARD_REQUIRED ON)

engine_bake_assets(app "${CMAKE_CURRENT_SOURCE_DIR}/Assets")
engine_build_shaders(app)
