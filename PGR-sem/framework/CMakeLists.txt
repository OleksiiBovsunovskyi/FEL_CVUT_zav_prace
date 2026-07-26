cmake_minimum_required (VERSION 3.10)

project (pgr)

set(OpenGL_GL_PREFERENCE GLVND)

if(APPLE)
  add_definitions(-Wno-deprecated)
  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/modules/")

  find_package(DevIL REQUIRED)
  find_package(assimp REQUIRED)

elseif(UNIX)
  # Linux: use system packages
  find_package(DevIL REQUIRED)
  find_package(assimp REQUIRED)
  find_package(X11 REQUIRED)
  find_library(GLX_LIBRARY GLX)

else()
  # Windows: use bundled libraries

  # Assimp — use the system-installed Assimp 5.x on x64 (assimp-vc143-mt)
  set(ASSIMP_SDK "C:/Program Files/Assimp")
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ASSIMP_LIB "${ASSIMP_SDK}/lib/x64/assimp-vc143-mt.lib")
    set(ASSIMP_DLL "${ASSIMP_SDK}/bin/x64/assimp-vc143-mt.dll")
    set(ASSIMP_INC "${ASSIMP_SDK}/include")
  else()
    set(ASSIMP_LIB "${CMAKE_CURRENT_SOURCE_DIR}/assimp/lib/assimp.lib")
    set(ASSIMP_DLL "")
    set(ASSIMP_INC "${CMAKE_CURRENT_SOURCE_DIR}/assimp/inc")
  endif()
  add_library(assimp_bundled SHARED IMPORTED GLOBAL)
  set_target_properties(assimp_bundled PROPERTIES
          IMPORTED_LOCATION "${ASSIMP_DLL}"
          IMPORTED_IMPLIB   "${ASSIMP_LIB}"
          INTERFACE_INCLUDE_DIRECTORIES "${ASSIMP_INC}"
  )
  set(ASSIMP_LIBRARIES assimp_bundled)

  # DevIL
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(DEVIL_LIB "${CMAKE_CURRENT_SOURCE_DIR}/DevIL/lib/DevIL64.lib")
  else()
    set(DEVIL_LIB "${CMAKE_CURRENT_SOURCE_DIR}/DevIL/lib/DevIL.lib")
  endif()
  add_library(devil STATIC IMPORTED GLOBAL)
  set_target_properties(devil PROPERTIES
          IMPORTED_LOCATION "${DEVIL_LIB}"
          INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/DevIL/inc"
  )
  set(IL_LIBRARIES devil)

endif()

find_package(OpenGL REQUIRED)

# pgr-framework
file (GLOB_RECURSE SOURCES src/*)

if(WIN32)
  add_library (${PROJECT_NAME} STATIC ${SOURCES})
else()
  add_library (${PROJECT_NAME} SHARED ${SOURCES})
endif()

target_compile_options(${PROJECT_NAME} PRIVATE -Wno-register)

target_include_directories (
        ${PROJECT_NAME} PUBLIC
        inc/
        ${OPENGL_INCLUDE_DIR}
        ${ASSIMP_INCLUDE_DIR}
        ${IL_INCLUDE_DIR}
)

target_link_libraries (
        ${PROJECT_NAME}
        PUBLIC
        ${OPENGL_LIBRARIES}
        ${ASSIMP_LIBRARIES}
        ${IL_LIBRARIES}
        $<$<PLATFORM_ID:Linux>:${X11_LIBRARIES}>
        $<$<PLATFORM_ID:Linux>:${GLX_LIBRARY}>
        $<$<PLATFORM_ID:Linux>:GL>
)