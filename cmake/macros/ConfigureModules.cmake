# This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

function(GetModulesBasePath variable)
  set(${variable} "${CMAKE_SOURCE_DIR}/modules" PARENT_SCOPE)
endfunction()

function(GetPathToModuleSource module variable)
  GetModulesBasePath(MODULE_BASE_PATH)
  set(${variable} "${MODULE_BASE_PATH}/${module}/src" PARENT_SCOPE)
endfunction()

function(GetPathToModuleConfig module variable)
  GetModulesBasePath(MODULE_BASE_PATH)
  set(${variable} "${MODULE_BASE_PATH}/${module}/conf" PARENT_SCOPE)
endfunction()

# Discovers drop-in modules: any child of modules/ that contains a src/ directory.
function(GetModuleSourceList variable)
  GetModulesBasePath(BASE_PATH)
  set(${variable})
  if(NOT IS_DIRECTORY "${BASE_PATH}")
    set(${variable} ${${variable}} PARENT_SCOPE)
    return()
  endif()

  file(GLOB LOCALE_MODULE_LIST RELATIVE
    ${BASE_PATH}
    ${BASE_PATH}/*)

  foreach(SOURCE_MODULE ${LOCALE_MODULE_LIST})
    GetPathToModuleSource(${SOURCE_MODULE} MODULE_SOURCE_PATH)
    if(IS_DIRECTORY ${MODULE_SOURCE_PATH})
      list(APPEND ${variable} ${SOURCE_MODULE})
    endif()
  endforeach()
  set(${variable} ${${variable}} PARENT_SCOPE)
endfunction()

# Cache variable holding linkage for a module (MODULE_MOD_EXAMPLE).
function(ModuleNameToVariable module variable)
  string(TOUPPER ${module} UPPER_NAME)
  string(REPLACE "-" "_" UPPER_NAME "${UPPER_NAME}")
  set(${variable} "MODULE_${UPPER_NAME}")
  set(${variable} ${${variable}} PARENT_SCOPE)
endfunction()
