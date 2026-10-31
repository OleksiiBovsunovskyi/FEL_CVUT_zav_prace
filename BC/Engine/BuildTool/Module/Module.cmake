#Compile with reduced bmi, mostly because of vulkan 
add_library(module_flags INTERFACE)
target_compile_options(module_flags INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-fmodules-reduced-bmi>)

#find python 3.11 for generator
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

# Cache variables are global; add_modules() runs in App's directory scope, 
#where Engine's normal variables don't exist.
set(MODULE_PYTHON "${Python3_EXECUTABLE}" CACHE INTERNAL "")
set(MODULE_GENERATOR "${CMAKE_CURRENT_LIST_DIR}/generate_modules.py" CACHE INTERNAL "")

# add_modules(<var> <dir>)
# Creates one target per .module.toml under <dir>, recursively; see generate_modules.py.
# <var> is set to the targets created.
macro(add_modules VAR DIR)
  # Output file, one per <var>.
  set(_MODULES_CMAKE "${CMAKE_CURRENT_BINARY_DIR}/modules/${VAR}.cmake")
  # Writes the add_library/target_* calls of every module, ending with set(<var> <targets>).
  execute_process(
          COMMAND "${MODULE_PYTHON}" "${MODULE_GENERATOR}" "${DIR}" "${VAR}" "${_MODULES_CMAKE}"
          RESULT_VARIABLE _MODULES_RESULT)
  if (NOT _MODULES_RESULT EQUAL 0)
    message(FATAL_ERROR "generate_modules.py failed for ${DIR}")
  endif ()
  # Re-glob modules after descriptor or sources change
  file(GLOB_RECURSE _MODULE_DESCRIPTORS CONFIGURE_DEPENDS "${DIR}/*.module.toml")
  file(GLOB_RECURSE _MODULE_SOURCES CONFIGURE_DEPENDS "${DIR}/*.ixx" "${DIR}/*.cpp")
  # An edit to a descriptor or to the generator reruns the configure.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_MODULE_DESCRIPTORS} "${MODULE_GENERATOR}")
  include("${_MODULES_CMAKE}")
endmacro()
