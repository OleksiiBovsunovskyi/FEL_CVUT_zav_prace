#the app: owns the scene, exports a C API (APP_API) for a managed UI host
cmake_minimum_required(VERSION 3.28)

project(app LANGUAGES C CXX)

if (NOT TARGET engine)
  add_subdirectory(../Engine "${CMAKE_CURRENT_BINARY_DIR}/Engine")
endif ()

add_library(app SHARED)
set_property(TARGET app PROPERTY PREFIX "")
set_property(TARGET app PROPERTY
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# One module per .module.toml under src/.
add_modules(APP_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(app PRIVATE ${APP_MODULES})
set_property(TARGET app PROPERTY CXX_STANDARD 23)
set_property(TARGET app PROPERTY CXX_STANDARD_REQUIRED ON)

engine_bake_assets(app "${CMAKE_CURRENT_SOURCE_DIR}/Assets")
engine_build_shaders(app)
