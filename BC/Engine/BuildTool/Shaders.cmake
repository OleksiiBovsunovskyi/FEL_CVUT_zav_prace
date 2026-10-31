find_program(SLANG_COMPILER
        NAMES slangc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
)
find_library(SLANG_LIBRARY
        NAMES slang
        HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib"
)
if(NOT SLANG_COMPILER OR NOT SLANG_LIBRARY)
  message(FATAL_ERROR
          "No slangc or slang.lib found. Install the Vulkan SDK "
          "(https://vulkan.lunarg.com/) and make sure VULKAN_SDK is set.")
endif()
message(STATUS "Shader compiler: ${SLANG_COMPILER}")

get_filename_component(SHADER_BUILD_DIR "${CMAKE_CURRENT_LIST_DIR}/../Tools/ShaderTypesGenerator" ABSOLUTE)
set(SHADER_BUILD_DIR "${SHADER_BUILD_DIR}" CACHE INTERNAL "")
set(SHADER_BUILD_PYTHON "${SHADER_BUILD_DIR}/.venv/Scripts/python.exe" CACHE INTERNAL "")
if(NOT EXISTS "${SHADER_BUILD_PYTHON}")
  message(FATAL_ERROR "No shader build venv. Create it with:\n"
          "  uv venv --python 3.12 \"${SHADER_BUILD_DIR}/.venv\"\n"
          "  uv pip install --python \"${SHADER_BUILD_PYTHON}\" -r \"${SHADER_BUILD_DIR}/requirements.txt\"")
endif()

function(engine_shader_root ROOT MODULES)
  add_modules(PRIVATE_MODULES "${ROOT}/Private")
  add_modules(SHARED_MODULES "${ROOT}/Shared")
  set_property(GLOBAL APPEND PROPERTY ENGINE_SHADER_ROOTS "${ROOT}")
  set(${MODULES} ${PRIVATE_MODULES} ${SHARED_MODULES} PARENT_SCOPE)
endfunction()

function(engine_build_shaders TARGET)
  get_property(ROOTS GLOBAL PROPERTY ENGINE_SHADER_ROOTS)
  set(ROOT_ARGS "")
  foreach(ROOT IN LISTS ROOTS)
    list(APPEND ROOT_ARGS --root "${ROOT}")
  endforeach()
  add_custom_target(${TARGET}-shaders
          COMMAND "${SHADER_BUILD_PYTHON}" "${SHADER_BUILD_DIR}/../UpdateShaders.py"
                  ${ROOT_ARGS}
                  --runtime-dir "$<TARGET_FILE_DIR:${TARGET}>"
                  --slangc "${SLANG_COMPILER}"
          COMMENT "Building shaders of ${TARGET}"
          VERBATIM
  )
  add_dependencies(${TARGET} ${TARGET}-shaders)
endfunction()

engine_shader_root("${VK_SHADER_ROOT}" ENGINE_SHADER_TYPE_MODULES)
