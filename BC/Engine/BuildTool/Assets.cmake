# Offline baker of the .gltf assets to the .pmma

#Build baker
add_modules(BAKE_TOOL_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/Tools/BakeTool")
add_executable(bake)
set_property(TARGET bake PROPERTY
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(bake PRIVATE ${BAKE_TOOL_MODULES})
set_property(TARGET bake PROPERTY CXX_STANDARD 23)
set_property(TARGET bake PROPERTY CXX_STANDARD_REQUIRED ON)

function(engine_bake_assets TARGET ASSET_DIR)
  #find assets to bake
  file(GLOB_RECURSE ASSET_SOURCES CONFIGURE_DEPENDS
          "${ASSET_DIR}/*.glb")

  list(REMOVE_DUPLICATES ASSET_SOURCES)
  #configure baked dir and baked asset list
  set(BAKED_DIR "${CMAKE_CURRENT_BINARY_DIR}/Assets")
  set(BAKED_ASSETS "")
  
  foreach (ASSET IN LISTS ASSET_SOURCES)
    #get asset source file name without extension
    get_filename_component(ASSET_NAME "${ASSET}" NAME_WE)
    #get asset source directory
    get_filename_component(SOURCE_DIR "${ASSET}" DIRECTORY)
    
    set(BAKED "${BAKED_DIR}/${ASSET_NAME}.pmma")
    #skip already baked assets.
    if(NOT EXISTS "${BAKED}" OR "${ASSET}" IS_NEWER_THAN "${BAKED}")
      message(STATUS "Baking ${ASSET_NAME}.pmma")
      add_custom_command(
              OUTPUT  "${BAKED}"
              COMMAND "$<TARGET_FILE:bake>" "${ASSET}" "${BAKED}"
              DEPENDS "${ASSET}" bake
              VERBATIM
      )
      list(APPEND BAKED_ASSETS "${BAKED}")
    endif ()
  endforeach ()

  #Copy baked assets to the build dir
  add_custom_target(${TARGET}-assets DEPENDS ${BAKED_ASSETS})
  add_dependencies(${TARGET} ${TARGET}-assets)
  add_custom_command(TARGET ${TARGET} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_directory
                  "${BAKED_DIR}" "$<TARGET_FILE_DIR:${TARGET}>/Assets"
          VERBATIM
  )
endfunction()
