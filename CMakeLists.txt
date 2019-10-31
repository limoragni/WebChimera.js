cmake_minimum_required(VERSION 3.13)

project(WebChimera.js)

# Set output to bin folder.
set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})

foreach (OUTPUT_CONFIG ${CMAKE_CONFIGURATION_TYPES})
  string(TOUPPER ${OUTPUT_CONFIG} OUTPUT_CONFIG)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUT_CONFIG} ${OUTPUT_DIR})
endforeach ()

# Set needed flags for each platform.
if (MSVC)
  foreach (flag_var
       CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
       CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    if (${flag_var} MATCHES "/MD")
      string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
    endif ()
  endforeach ()
else ()
  add_definitions(-std=c++11)
endif ()

# Include submodules.
include_directories(${CMAKE_JS_INC} deps)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/deps/libvlc_wrapper)

# Add source files to the library.
file(GLOB SOURCE_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  src/[^.]*.cpp
  src/[^.]*.h
  README.md
  package.json
)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})

# Set library output suffix.
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")

# Add Visual Studio properties for testing.
if (MSVC)
  find_program(NODE_PATH node)
  set_property(
    TARGET ${PROJECT_NAME}
    PROPERTY VS_DEBUGGER_COMMAND ${NODE_PATH}
  )

  set_property(
    TARGET ${PROJECT_NAME}
    PROPERTY VS_DEBUGGER_COMMAND_ARGUMENTS ${CMAKE_CURRENT_SOURCE_DIR}/test/test.js
  )

  set_property(
    TARGET ${PROJECT_NAME}
    PROPERTY VS_DEBUGGER_ENVIRONMENT VLC_PLUGIN_PATH=${CMAKE_CURRENT_SOURCE_DIR}/bin/plugins
  )

  set_property(
    DIRECTORY ${PROJECT_SOURCE_DIR}
    PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME}
  )
endif ()

# Link submodules to library.
target_link_libraries(${PROJECT_NAME} ${CMAKE_JS_LIB} libvlc_wrapper)
