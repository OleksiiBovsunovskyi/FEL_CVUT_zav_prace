#engine library

cmake_minimum_required(VERSION 3.28)
project(engine LANGUAGES C CXX)

# Source directory. 
set(VK_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")

include(cmake/Dependencies.cmake)
include(cmake/Modules.cmake)
include(cmake/ThirdPartyLibs.cmake)
include(cmake/Shaders.cmake)
include(cmake/Assets.cmake)
include(cmake/Module.cmake)

# One module per .module.cmake under src/.
add_modules(ENGINE_MODULES "${VK_SRC}")

# Every engine module.
add_library(engine INTERFACE)
target_link_libraries(engine INTERFACE ${ENGINE_MODULES})
