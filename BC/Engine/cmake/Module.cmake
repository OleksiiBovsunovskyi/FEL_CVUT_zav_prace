# ---------------------------------------------------------------------------
# add_modules(<var> <dir>)
# Includes every .module.cmake under <dir>.
# Each module() called from them is appended to <var>.
# ---------------------------------------------------------------------------
macro(add_modules VAR DIR)
  set(${VAR} "")
  set(MODULE_LIST ${VAR})
  file(GLOB_RECURSE _MODULE_FILES CONFIGURE_DEPENDS "${DIR}/*.module.cmake")
  foreach (_MODULE_FILE IN LISTS _MODULE_FILES)
    include("${_MODULE_FILE}")
  endforeach ()
endmacro()

# ---------------------------------------------------------------------------
# module(<name> [OBJECT] [WHOLE_ARCHIVE] [DEPENDS <target>...] [PRIVATE_DEPENDS <target>...])
# One STATIC library from every .ixx and .cpp under the calling .module.cmake's
# directory, subdirectories included.
# .ixx files are module interfaces; .cpp files are implementation units.
# A module imports only modules of the targets in DEPENDS and PRIVATE_DEPENDS.
# OBJECT makes an OBJECT library; its objects reach only targets linking it directly.
# WHOLE_ARCHIVE keeps objects no other object references, such as static-init checks.
# ---------------------------------------------------------------------------
function(module NAME)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "OBJECT;WHOLE_ARCHIVE" "" "DEPENDS;PRIVATE_DEPENDS")
  set(DIR "${CMAKE_CURRENT_LIST_DIR}")
  file(GLOB_RECURSE INTERFACES CONFIGURE_DEPENDS "${DIR}/*.ixx")
  file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${DIR}/*.cpp")

  # Files under a subdirectory with its own .module.cmake belong to that module.
  file(GLOB_RECURSE NESTED_MODULE_FILES CONFIGURE_DEPENDS "${DIR}/*.module.cmake")
  list(REMOVE_ITEM NESTED_MODULE_FILES "${CMAKE_CURRENT_LIST_FILE}")
  foreach (NESTED_MODULE_FILE IN LISTS NESTED_MODULE_FILES)
    cmake_path(GET NESTED_MODULE_FILE PARENT_PATH NESTED_DIR)
    foreach (FILE IN LISTS INTERFACES SOURCES)
      string(FIND "${FILE}" "${NESTED_DIR}/" AT)
      if (AT EQUAL 0)
        list(REMOVE_ITEM INTERFACES "${FILE}")
        list(REMOVE_ITEM SOURCES "${FILE}")
      endif ()
    endforeach ()
  endforeach ()

  if (ARG_OBJECT)
    add_library(${NAME} OBJECT ${SOURCES})
  else ()
    add_library(${NAME} STATIC ${SOURCES})
  endif ()
  if (INTERFACES)
    target_sources(${NAME} PUBLIC
            FILE_SET CXX_MODULES
            BASE_DIRS "${DIR}"
            FILES ${INTERFACES})
  endif ()
  target_link_libraries(${NAME} PUBLIC ${ARG_DEPENDS} PRIVATE ${ARG_PRIVATE_DEPENDS})
  set_property(TARGET ${NAME} PROPERTY CXX_STANDARD 23)
  set_property(TARGET ${NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
  # Unhandled enum values in a switch are errors.
  target_compile_options(${NAME} PRIVATE -Werror=switch)

  if (ARG_WHOLE_ARCHIVE)
    set(${MODULE_LIST} ${${MODULE_LIST}} "$<LINK_LIBRARY:WHOLE_ARCHIVE,${NAME}>" PARENT_SCOPE)
  else ()
    set(${MODULE_LIST} ${${MODULE_LIST}} ${NAME} PARENT_SCOPE)
  endif ()
endfunction()
