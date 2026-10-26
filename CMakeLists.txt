# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Generated config lives in the build dir; edit sdkconfig.defaults / sdkconfig.local instead
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

# Later files override earlier ones; sdkconfig.local is optional (not committed)
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_SOURCE_DIR}/sdkconfig.local")
  list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.local")
endif()

# ESP-IDF only applies defaults to options missing from the generated sdkconfig,
# so regenerate it whenever a defaults file changes
foreach(defaults ${SDKCONFIG_DEFAULTS})
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/${defaults}")
  if("${CMAKE_SOURCE_DIR}/${defaults}" IS_NEWER_THAN "${SDKCONFIG}")
    file(REMOVE "${SDKCONFIG}")
  endif()
endforeach()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vogon-sensor)
