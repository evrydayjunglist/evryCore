# This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

message("")

GetModuleSourceList(MODULES_MODULE_LIST)

if(MODULES MATCHES "static")
  set(MODULES_DEFAULT_LINKAGE "static")
else()
  set(MODULES_DEFAULT_LINKAGE "disabled")
endif()

foreach(SOURCE_MODULE ${MODULES_MODULE_LIST})
  ModuleNameToVariable(${SOURCE_MODULE} MODULE_MODULE_VARIABLE)
  if(${MODULE_MODULE_VARIABLE} STREQUAL "default")
    set(${MODULE_MODULE_VARIABLE} ${MODULES_DEFAULT_LINKAGE})
  endif()

  if(${MODULE_MODULE_VARIABLE} MATCHES "static")
    list(APPEND MODULE_GRAPH_KEYS worldserver)
    set(MODULE_GRAPH_VALUE_DISPLAY_worldserver worldserver)
    list(APPEND MODULE_GRAPH_VALUE_CONTAINS_MODULES_worldserver ${SOURCE_MODULE})
  else()
    list(APPEND MODULE_GRAPH_KEYS disabled)
    set(MODULE_GRAPH_VALUE_DISPLAY_disabled disabled)
    list(APPEND MODULE_GRAPH_VALUE_CONTAINS_MODULES_disabled ${SOURCE_MODULE})
  endif()
endforeach()

list(SORT MODULE_GRAPH_KEYS)
list(REMOVE_DUPLICATES MODULE_GRAPH_KEYS)

message("* Modules configuration (${MODULES}):
  |")

foreach(MODULE_GRAPH_KEY ${MODULE_GRAPH_KEYS})
  if(NOT MODULE_GRAPH_KEY STREQUAL "disabled")
    message("  +- ${MODULE_GRAPH_VALUE_DISPLAY_${MODULE_GRAPH_KEY}}")
  else()
    message("  |  ${MODULE_GRAPH_VALUE_DISPLAY_${MODULE_GRAPH_KEY}}")
  endif()
  foreach(MODULE_GRAPH_PROJECT_ENTRY ${MODULE_GRAPH_VALUE_CONTAINS_MODULES_${MODULE_GRAPH_KEY}})
    message("  |   +- ${MODULE_GRAPH_PROJECT_ENTRY}")
  endforeach()
  message("  |")
endforeach()

message("")

function(ConfigureModulesLoader SCRIPTLOADER_NAME LOADER_OUT)
  unset(EVRY_MODULES_FORWARD_DECL)
  unset(EVRY_MODULES_INVOKE)

  foreach(LOCALE_SCRIPT_MODULE ${ARGN})
    string(REGEX REPLACE "-" "_" LOCALE_SCRIPT_MODULE ${LOCALE_SCRIPT_MODULE})
    set(LOADER_FUNCTION "Add${LOCALE_SCRIPT_MODULE}Scripts()")
    set(EVRY_MODULES_FORWARD_DECL
      "${EVRY_MODULES_FORWARD_DECL}void ${LOADER_FUNCTION};\n")
    set(EVRY_MODULES_INVOKE
      "${EVRY_MODULES_INVOKE}    ${LOADER_FUNCTION};\n")
  endforeach()

  set(GENERATED_LOADER ${CMAKE_CURRENT_BINARY_DIR}/gen_scriptloader/${SCRIPTLOADER_NAME}/ModulesLoader.cpp)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ModulesLoader.cpp.in.cmake ${GENERATED_LOADER})
  set(${LOADER_OUT} ${GENERATED_LOADER} PARENT_SCOPE)
endfunction()

set(ENABLED_MODULE_LIST)
set(STATIC_GAME_MODULES)
set(MODULE_CONFIG_COPY_COMMANDS)

add_library(modules STATIC)

foreach(SOURCE_MODULE ${MODULES_MODULE_LIST})
  GetPathToModuleSource(${SOURCE_MODULE} MODULE_SOURCE_PATH)
  ModuleNameToVariable(${SOURCE_MODULE} MODULE_MODULE_VARIABLE)

  if(${MODULE_MODULE_VARIABLE} STREQUAL "static")
    list(APPEND STATIC_GAME_MODULES ${SOURCE_MODULE})
    list(APPEND ENABLED_MODULE_LIST ${SOURCE_MODULE})
    CollectAndAddSourceFiles(modules ${MODULE_SOURCE_PATH} BASE_DIR ${CMAKE_SOURCE_DIR}/modules HEADER_VISIBILITY PRIVATE)

    GetPathToModuleConfig(${SOURCE_MODULE} MODULE_CONFIG_PATH)
    if(IS_DIRECTORY "${MODULE_CONFIG_PATH}")
      file(GLOB MODULE_CONFIG_LIST "${MODULE_CONFIG_PATH}/*.conf.dist")
      foreach(configFile ${MODULE_CONFIG_LIST})
        get_filename_component(configFileName ${configFile} NAME)
        set_source_files_properties("${configFile}" PROPERTIES HEADER_FILE_ONLY ON)
        target_sources(modules PRIVATE "${configFile}")
        list(APPEND MODULE_CONFIG_COPY_COMMANDS
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${configFile}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/modules/${configFileName}")
      endforeach()
    endif()
  endif()
endforeach()

ConfigureModulesLoader("static" MODULES_PRIVATE_SCRIPTLOADER ${STATIC_GAME_MODULES})

string(REPLACE ";" "," ENABLED_MODULE_LIST_CSV "${ENABLED_MODULE_LIST}")
if(ENABLED_MODULE_LIST_CSV)
  set(ENABLED_MODULE_LIST_CSV "${ENABLED_MODULE_LIST_CSV},")
endif()
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/EnabledModulesList.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/EnabledModulesList.h
  @ONLY)

target_sources(modules
  PRIVATE
    ${MODULES_PRIVATE_SCRIPTLOADER})

target_sources(modules
  PUBLIC
    FILE_SET HEADERS FILES ModulesScriptLoader.h)

target_link_libraries(modules
  PRIVATE
    trinity-core-interface
  PUBLIC
    game-interface)

target_include_directories(modules
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})

set_target_properties(modules
  PROPERTIES
    COMPILE_WARNING_AS_ERROR ${WITH_WARNINGS_AS_ERRORS}
    FOLDER "modules")

foreach(SOURCE_MODULE ${MODULES_MODULE_LIST})
  ModuleNameToVariable(${SOURCE_MODULE} MODULE_MODULE_VARIABLE)
  if(${MODULE_MODULE_VARIABLE} STREQUAL "static")
    include("${CMAKE_SOURCE_DIR}/modules/${SOURCE_MODULE}/${SOURCE_MODULE}.cmake" OPTIONAL)
  endif()
endforeach()

set_property(GLOBAL PROPERTY EVRY_MODULE_CONFIG_COPY_COMMANDS ${MODULE_CONFIG_COPY_COMMANDS})

message("")
