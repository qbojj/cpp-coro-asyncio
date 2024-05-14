cmake_minimum_required(VERSION 3.23)

project(coro-asyncio)

set(SOURCES
  src/io_engine.cpp
  src/utils.cpp
)

set(HEADERS
  include/io_engine.hpp
  include/utils.hpp
)

add_library(coro-asyncio ${SOURCES} ${HEADERS})
add_library(coro::asyncio ALIAS coro-asyncio)

set_target_properties(coro-asyncio 
PROPERTIES
  PUBLIC_HEADER "${HEADERS}"
)

target_include_directories(coro-asyncio
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/coro-asyncio>
)

target_compile_features(coro-asyncio PUBLIC cxx_std_20)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/coro-asyncio-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/coro-asyncio-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/coro-asyncio
)

export(TARGETS coro-asyncio
  FILE ${CMAKE_CURRENT_BINARY_DIR}/coro-asyncio-targets.cmake
)

install(TARGETS coro-asyncio
  EXPORT coro-asyncio-targets
  LIBRARY
    COMPONENT lib
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE
    COMPONENT lib
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER
    COMPONENT dev
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/coro-asyncio
)

install(EXPORT coro-asyncio-targets
  FILE coro-asyncio-targets.cmake
  NAMESPACE coro::
  COMPONENT dev
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/coro-asyncio
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/coro-asyncio-config.cmake
  COMPONENT dev
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/coro-asyncio
)