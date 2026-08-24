# This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.

option(PLAYERBOTS_BUILD_EXECUTABLE "Publish the self-contained playerbots coordinator" ON)

if(NOT PLAYERBOTS_BUILD_EXECUTABLE)
  return()
endif()

find_program(PLAYERBOTS_DOTNET_EXECUTABLE NAMES dotnet REQUIRED)

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" PLAYERBOTS_SYSTEM_PROCESSOR)
if(PLAYERBOTS_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
  set(PLAYERBOTS_RUNTIME_ARCH arm64)
else()
  set(PLAYERBOTS_RUNTIME_ARCH x64)
endif()

if(WIN32)
  set(PLAYERBOTS_RUNTIME_ID "win-${PLAYERBOTS_RUNTIME_ARCH}")
  set(PLAYERBOTS_EXECUTABLE_NAME playerbots.exe)
elseif(APPLE)
  set(PLAYERBOTS_RUNTIME_ID "osx-${PLAYERBOTS_RUNTIME_ARCH}")
  set(PLAYERBOTS_EXECUTABLE_NAME playerbots)
else()
  set(PLAYERBOTS_RUNTIME_ID "linux-${PLAYERBOTS_RUNTIME_ARCH}")
  set(PLAYERBOTS_EXECUTABLE_NAME playerbots)
endif()

set(PLAYERBOTS_DOTNET_ROOT "${CMAKE_CURRENT_LIST_DIR}/playerbots")
set(PLAYERBOTS_HOST_PROJECT "${PLAYERBOTS_DOTNET_ROOT}/src/Playerbots.Host/Playerbots.Host.csproj")
set(PLAYERBOTS_PUBLISH_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/playerbots-publish/$<CONFIG>")
set(PLAYERBOTS_PUBLISHED_EXECUTABLE "${PLAYERBOTS_PUBLISH_DIRECTORY}/${PLAYERBOTS_EXECUTABLE_NAME}")
set(PLAYERBOTS_RUNTIME_EXECUTABLE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${PLAYERBOTS_EXECUTABLE_NAME}")

file(GLOB PLAYERBOTS_DOTNET_SOURCES CONFIGURE_DEPENDS
  "${PLAYERBOTS_DOTNET_ROOT}/src/Playerbots.Host/*.cs"
  "${PLAYERBOTS_DOTNET_ROOT}/src/Playerbots.Protocol/*.cs")
list(APPEND PLAYERBOTS_DOTNET_SOURCES
  "${PLAYERBOTS_HOST_PROJECT}"
  "${PLAYERBOTS_DOTNET_ROOT}/src/Playerbots.Host/Assets/playerbots.ico"
  "${PLAYERBOTS_DOTNET_ROOT}/src/Playerbots.Protocol/Playerbots.Protocol.csproj"
  "${PLAYERBOTS_DOTNET_ROOT}/Directory.Build.props")

add_custom_command(
  OUTPUT "${PLAYERBOTS_RUNTIME_EXECUTABLE}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${PLAYERBOTS_PUBLISH_DIRECTORY}"
  COMMAND ${CMAKE_COMMAND} -E env
    DOTNET_CLI_TELEMETRY_OPTOUT=1
    DOTNET_NOLOGO=1
    "${PLAYERBOTS_DOTNET_EXECUTABLE}" publish "${PLAYERBOTS_HOST_PROJECT}"
      --configuration $<IF:$<CONFIG:Debug>,Debug,Release>
      --runtime "${PLAYERBOTS_RUNTIME_ID}"
      --self-contained true
      --output "${PLAYERBOTS_PUBLISH_DIRECTORY}"
      -p:PublishSingleFile=true
      -p:IncludeNativeLibrariesForSelfExtract=true
      -p:PublishTrimmed=false
      -p:DebugSymbols=false
      -p:DebugType=None
  COMMAND ${CMAKE_COMMAND} -E copy "${PLAYERBOTS_PUBLISHED_EXECUTABLE}" "${PLAYERBOTS_RUNTIME_EXECUTABLE}"
  DEPENDS ${PLAYERBOTS_DOTNET_SOURCES}
  COMMENT "Publishing self-contained ${PLAYERBOTS_EXECUTABLE_NAME}"
  VERBATIM
  USES_TERMINAL)

add_custom_target(playerbots_executable DEPENDS "${PLAYERBOTS_RUNTIME_EXECUTABLE}")
add_dependencies(modules playerbots_executable)
set_target_properties(playerbots_executable PROPERTIES FOLDER "modules")
