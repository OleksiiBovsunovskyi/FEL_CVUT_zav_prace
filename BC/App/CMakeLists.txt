#the app: owns the scene, exports a C API (PGR_API) for a managed UI host
cmake_minimum_required(VERSION 3.28)

project(app LANGUAGES C CXX)

if (NOT TARGET engine)
  add_subdirectory(../Engine "${CMAKE_CURRENT_BINARY_DIR}/Engine")
endif ()

add_library(pgr_embed SHARED)
set_property(TARGET pgr_embed PROPERTY PREFIX "")
set_property(TARGET pgr_embed PROPERTY
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# One module per .module.toml under src/.
add_modules(EMBED_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pgr_embed PRIVATE ${EMBED_MODULES})
set_property(TARGET pgr_embed PROPERTY CXX_STANDARD 23)
set_property(TARGET pgr_embed PROPERTY CXX_STANDARD_REQUIRED ON)

engine_bake_assets(pgr_embed "${CMAKE_CURRENT_SOURCE_DIR}/Assets")
engine_build_shaders(pgr_embed)
