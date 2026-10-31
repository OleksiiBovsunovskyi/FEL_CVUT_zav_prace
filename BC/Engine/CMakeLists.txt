#engine library

cmake_minimum_required(VERSION 3.28)
project(engine LANGUAGES C CXX)

# Source directory. 
set(VK_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# Private/, Shared/ and Public/ shader folders; see Tools/UpdateShaders.py.
set(VK_SHADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Shaders")

include(BuildTool/Dependencies.cmake)
include(BuildTool/Module/Module.cmake)
include(BuildTool/Shaders.cmake)
include(BuildTool/Assets.cmake)

# One module per .module.toml under src/.
add_modules(ENGINE_MODULES "${VK_SRC}")

# Every engine module.
add_library(engine INTERFACE)
target_link_libraries(engine INTERFACE ${ENGINE_SHADER_TYPE_MODULES} ${ENGINE_MODULES})
