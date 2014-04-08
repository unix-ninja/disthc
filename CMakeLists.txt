cmake_minimum_required (VERSION 2.6)
cmake_policy (VERSION 2.6)

project(disthc)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

set (DATADIR "${CMAKE_INSTALL_PREFIX}/share")
set (PKGDATADIR "${DATADIR}/disthc")
set (GETTEXT_PACKAGE "disthc")
set (RELEASE_NAME "Alpha")
set (VERSION "0.1")
set (VERSION_INFO "Release")
set (CMAKE_C_FLAGS "-ggdb")
set (PREFIX ${CMAKE_INSTALL_PREFIX})
set (DOLLAR "$")
set(CMAKE_MAN_DIR "man" CACHE STRING
  "Install location for man pages (relative to prefix).")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
# Mac OS X specific code
SET(OperatingSystem "Mac OS X")
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

add_subdirectory (src)

# Copy the cfg files
file(COPY cfg/master.properties DESTINATION ${CMAKE_BINARY_DIR}/bin/)
file(COPY cfg/slave.properties DESTINATION ${CMAKE_BINARY_DIR}/bin/)

