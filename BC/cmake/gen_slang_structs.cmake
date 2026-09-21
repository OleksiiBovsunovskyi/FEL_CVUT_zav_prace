# ===========================================================================
# Translates the shader-facing records in Core/GPUTypes.ixx into Slang.
#
#   cmake -DINPUT=<GPUTypes.ixx> -DOUTPUT=<gpu_types.slang>
#         -P gen_slang_structs.cmake
#
# The C++ module is the single source of truth; the Slang module is a build
# artifact. Handles the subset those records are written in:
#
#   export struct [alignas(N)] GPU*     one field per line
#   export enum <name> : uint32_t       one enumerator per line
#   export constexpr uint32_t <name>    literal, or numeric_limits<>::max()
#
# Anything else in the module is skipped, so type aliases and the push structs
# stay hand-written on the Slang side. An `export constexpr` it cannot
# translate is an error rather than a silent omission.
#
# Comments come across as // and /// only: the Slang IDE plugin does not
# handle block comments.
# ===========================================================================

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "gen_slang_structs: INPUT and OUTPUT are required")
endif()

file(READ "${INPUT}" SOURCE)

# Every C++ statement ends in a semicolon, which is also CMake's list
# separator; escaping first keeps a line from being split into fields. Note
# that list expansion turns these back into plain semicolons inside foreach.
string(REPLACE ";" "\\;" SOURCE "${SOURCE}")
string(REPLACE "\r" "" SOURCE "${SOURCE}")
string(REPLACE "\n" ";" LINES "${SOURCE}")

set(OUT "")
macro(emit TEXT)
  string(APPEND OUT "${TEXT}\n")
endmacro()

emit("// Generated from Core/GPUTypes.ixx by cmake/gen_slang_structs.cmake.")
emit("// Do not edit: change the C++ record and rebuild.")
emit("//")
emit("// Slang lays a pointed-to struct out in natural layout, which is what the")
emit("// C++ records already use, so no layout qualifiers are needed. The build")
emit("// also compares every offset here against offsetof() on the C++ side.")
emit("")
emit("module gpu_types;")
emit("")

# One of NONE, STRUCT, ENUM, CONSTANT; plus a flag for a wrapped initializer.
set(STATE "NONE")
set(IN_WRAPPED_INITIALIZER FALSE)
set(PENDING_COMMENT "")
set(CONSTANT_NAME "")

# Turns a C++ field type plus array arity into the Slang spelling. glm's vector
# and matrix types map one-to-one; the scalars keep their array form.
function(slang_type CPP_TYPE ARITY FIELD OUT_VAR)
  # `export using Name = GpuPtr<T>;` earlier in the module, collected below.
  foreach(ENTRY IN LISTS POINTER_ALIASES)
    if(ENTRY MATCHES "^${CPP_TYPE}=(.+)$")
      set(${OUT_VAR} "${CMAKE_MATCH_1}*" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # A nested record keeps its name.
  if(CPP_TYPE MATCHES "^GPU[A-Za-z0-9_]*$")
    set(${OUT_VAR} "${CPP_TYPE}" PARENT_SCOPE)
    return()
  endif()
  # A typed device address is a pointer on the Slang side.
  if(CPP_TYPE MATCHES "^GpuPtr<([A-Za-z0-9_]+)>$")
    set(${OUT_VAR} "${CMAKE_MATCH_1}*" PARENT_SCOPE)
    return()
  endif()
  if(CPP_TYPE MATCHES "^glm::(vec|uvec|ivec)([234])$")
    if(CMAKE_MATCH_1 STREQUAL "vec")
      set(${OUT_VAR} "float${CMAKE_MATCH_2}" PARENT_SCOPE)
    elseif(CMAKE_MATCH_1 STREQUAL "uvec")
      set(${OUT_VAR} "uint${CMAKE_MATCH_2}" PARENT_SCOPE)
    else()
      set(${OUT_VAR} "int${CMAKE_MATCH_2}" PARENT_SCOPE)
    endif()
    return()
  endif()
  if(CPP_TYPE MATCHES "^glm::mat([234])$")
    set(${OUT_VAR} "float${CMAKE_MATCH_1}x${CMAKE_MATCH_1}" PARENT_SCOPE)
    return()
  endif()

  if(CPP_TYPE STREQUAL "float")
    set(SCALAR "float")
  elseif(CPP_TYPE STREQUAL "uint32_t")
    set(SCALAR "uint")
  elseif(CPP_TYPE STREQUAL "int32_t")
    set(SCALAR "int")
  else()
    message(FATAL_ERROR "gen_slang_structs: unmapped type '${CPP_TYPE}' on '${FIELD}'")
  endif()

  if(NOT ARITY)
    set(${OUT_VAR} "${SCALAR}" PARENT_SCOPE)
  else()
    set(${OUT_VAR} "${SCALAR}[${ARITY}]" PARENT_SCOPE)
  endif()
endfunction()

foreach(LINE IN LISTS LINES)
  string(REGEX REPLACE "^[ \t]+" "" TRIMMED "${LINE}")
  string(REGEX REPLACE "[ \t]+$" "" TRIMMED "${TRIMMED}")

  # A field's initializer may span lines; drop the rest of it.
  if(IN_WRAPPED_INITIALIZER)
    if(TRIMMED MATCHES ";")
      set(IN_WRAPPED_INITIALIZER FALSE)
    endif()
    continue()
  endif()

  # Trailing // comment on a declaration, kept on the generated line.
  set(TRAILING "")
  if(TRIMMED MATCHES "^[^/]+(//.*)$")
    set(TRAILING "  ${CMAKE_MATCH_1}")
  endif()

  if(STATE STREQUAL "CONSTANT")
    if(TRIMMED MATCHES "numeric_limits<uint32_t>::max\\(\\)")
      emit("public static const uint ${CONSTANT_NAME} = 0xffffffffu;")
    elseif(TRIMMED MATCHES "^([0-9][0-9A-Fa-fxu]*)")
      emit("public static const uint ${CONSTANT_NAME} = ${CMAKE_MATCH_1}u;")
    else()
      message(FATAL_ERROR
              "gen_slang_structs: cannot translate the value of '${CONSTANT_NAME}'")
    endif()
    emit("")
    set(STATE "NONE")
    continue()
  endif()

  if(STATE STREQUAL "NONE")
    # A comment directly above a record travels with it.
    if(TRIMMED MATCHES "^///(.*)$")
      string(APPEND PENDING_COMMENT "///${CMAKE_MATCH_1}\n")
      continue()
    elseif(TRIMMED MATCHES "^/\\*\\*?$")
      continue()
    elseif(TRIMMED MATCHES "^\\*/$")
      continue()
    elseif(TRIMMED MATCHES "^\\*[ \t]?(.*)$")
      set(COMMENT_BODY "${CMAKE_MATCH_1}")
      if(COMMENT_BODY STREQUAL "")
        string(APPEND PENDING_COMMENT "///\n")
      else()
        string(APPEND PENDING_COMMENT "/// ${COMMENT_BODY}\n")
      endif()
      continue()
    endif()

    if(TRIMMED MATCHES "^export[ \t]+using[ \t]+([A-Za-z0-9_]+)[ \t]*=[ \t]*GpuPtr<([A-Za-z0-9_]+)>")
      list(APPEND POINTER_ALIASES "${CMAKE_MATCH_1}=${CMAKE_MATCH_2}")
      set(PENDING_COMMENT "")
      continue()
    endif()

    if(TRIMMED MATCHES "^export[ \t]+struct[ \t]+(alignas\\([0-9]+\\)[ \t]+)?(GPU[A-Za-z0-9_]*)[ \t]*\\{")
      emit("${PENDING_COMMENT}public struct ${CMAKE_MATCH_2} {")
      set(PENDING_COMMENT "")
      set(STATE "STRUCT")
    elseif(TRIMMED MATCHES "^export[ \t]+enum[ \t]+[A-Za-z0-9_]+[ \t]*:[ \t]*uint32_t[ \t]*\\{")
      string(APPEND OUT "${PENDING_COMMENT}")
      set(PENDING_COMMENT "")
      set(STATE "ENUM")
    elseif(TRIMMED MATCHES "^export[ \t]+constexpr[ \t]+uint32_t[ \t]+([A-Za-z0-9_]+)[ \t]*=[ \t]*(.*)$")
      set(CONSTANT_NAME "${CMAKE_MATCH_1}")
      # An empty capture group leaves CMAKE_MATCH_2 undefined, which compares
      # as its own name; going through a real variable makes it empty.
      set(CONSTANT_VALUE "${CMAKE_MATCH_2}")
      set(PENDING_COMMENT "")
      # The value is on this line, or on the next when the declaration wraps.
      if(CONSTANT_VALUE STREQUAL "")
        set(STATE "CONSTANT")
      elseif(CONSTANT_VALUE MATCHES "numeric_limits<uint32_t>::max\\(\\)")
        emit("public static const uint ${CONSTANT_NAME} = 0xffffffffu;\n")
      elseif(CONSTANT_VALUE MATCHES "^([0-9][0-9A-Fa-fxu]*)")
        emit("public static const uint ${CONSTANT_NAME} = ${CMAKE_MATCH_1}u;\n")
      else()
        message(FATAL_ERROR
                "gen_slang_structs: cannot translate the value of '${CONSTANT_NAME}'")
      endif()
    else()
      set(PENDING_COMMENT "")
    endif()
    continue()
  endif()

  if(TRIMMED MATCHES "^\\}")
    if(STATE STREQUAL "STRUCT")
      emit("};")
    endif()
    emit("")
    set(STATE "NONE")
    continue()
  endif()

  if(STATE STREQUAL "ENUM")
    if(TRIMMED MATCHES "^([A-Za-z0-9_]+)[ \t]*=[ \t]*([^,]+),")
      emit("public static const uint ${CMAKE_MATCH_1} = ${CMAKE_MATCH_2};")
    endif()
    continue()
  endif()

  # Comments and blank lines inside a record are kept, block form rewritten.
  if(TRIMMED STREQUAL "")
    emit("")
    continue()
  endif()
  if(TRIMMED MATCHES "^///(.*)$")
    emit("    ///${CMAKE_MATCH_1}")
    continue()
  endif()
  if(TRIMMED MATCHES "^//(.*)$")
    emit("    //${CMAKE_MATCH_1}")
    continue()
  endif()
  if(TRIMMED MATCHES "^/\\*+[ \t]*(.*[^ \t*])[ \t]*\\*+/$")
    emit("    // ${CMAKE_MATCH_1}")
    continue()
  endif()

  if(NOT TRIMMED MATCHES "^([A-Za-z0-9_:<>]+)[ \t]+([A-Za-z0-9_]+)(\\[([0-9]+)\\])?")
    message(FATAL_ERROR "gen_slang_structs: cannot parse field '${TRIMMED}'")
  endif()
  set(CPP_TYPE "${CMAKE_MATCH_1}")
  set(FIELD    "${CMAKE_MATCH_2}")
  set(ARITY    "${CMAKE_MATCH_4}")

  slang_type("${CPP_TYPE}" "${ARITY}" "${FIELD}" GLSL_TYPE)

  # Field initializers have no Slang equivalent and are dropped.
  if(GLSL_TYPE MATCHES "^(.+)\\[([0-9]+)\\]$")
    emit("    public ${CMAKE_MATCH_1} ${FIELD}[${CMAKE_MATCH_2}];${TRAILING}")
  else()
    emit("    public ${GLSL_TYPE} ${FIELD};${TRAILING}")
  endif()

  if(NOT TRIMMED MATCHES ";")
    set(IN_WRAPPED_INITIALIZER TRUE)
  endif()
endforeach()

file(WRITE "${OUTPUT}" "${OUT}")
