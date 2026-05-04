cmake_minimum_required(VERSION 3.16)

project(PortForwardX
  VERSION 0.1.0
  DESCRIPTION "Port forward tool / library"
  LANGUAGES CXX
)

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(cmake/CompilerWarnings.cmake)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PORTFORWARDX_BUILD_EXAMPLES "Build example executables" ON)
option(PORTFORWARDX_ENABLE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(PORTFORWARDX_BUILD_TESTS "Build tests" ON)
option(PORTFORWARDX_INSTALL_RUNTIME_DEPS "Install runtime third-party shared dependencies" ON)
set(PORTFORWARDX_THIRD_PARTY_LINK "STATIC" CACHE STRING "Third-party linkage mode: STATIC or SHARED")
set_property(CACHE PORTFORWARDX_THIRD_PARTY_LINK PROPERTY STRINGS STATIC SHARED)
option(PORTFORWARDX_USE_ONLY_LOCAL_THIRD_PARTY "Only search third_party install tree" ON)

set(PORTFORWARDX_THIRD_PARTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/install")
set(PORTFORWARDX_BOOST_ROOT "${PORTFORWARDX_THIRD_PARTY_ROOT}/boost")

if(PORTFORWARDX_THIRD_PARTY_LINK STREQUAL "STATIC")
  set(Boost_USE_STATIC_LIBS ON)
  set(Boost_USE_STATIC_RUNTIME ON)
elseif(PORTFORWARDX_THIRD_PARTY_LINK STREQUAL "SHARED")
  set(Boost_USE_STATIC_LIBS OFF)
  set(Boost_USE_STATIC_RUNTIME OFF)
else()
  message(FATAL_ERROR "PORTFORWARDX_THIRD_PARTY_LINK must be STATIC or SHARED")
endif()

if(MSVC)
  if(PORTFORWARDX_THIRD_PARTY_LINK STREQUAL "STATIC")
    # third_party/build_boost.* uses runtime-link=static (/MT); default VS is /MD.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  elseif(PORTFORWARDX_THIRD_PARTY_LINK STREQUAL "SHARED")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:Debug>")
  endif()
endif()

set(Boost_ROOT "${PORTFORWARDX_BOOST_ROOT}")
list(PREPEND CMAKE_PREFIX_PATH "${PORTFORWARDX_BOOST_ROOT}")
if(PORTFORWARDX_USE_ONLY_LOCAL_THIRD_PARTY)
  set(Boost_NO_SYSTEM_PATHS ON)
endif()

find_package(Boost 1.83 CONFIG COMPONENTS program_options)
if(NOT Boost_FOUND)
  message(FATAL_ERROR
    "Boost not found in ${PORTFORWARDX_BOOST_ROOT}. "
    "Build it first via third_party/build_boost.sh or third_party/build_boost.ps1")
endif()

add_library(portforwardx_core STATIC
  src/version.cpp
  src/net/dns_resolver.cpp
  src/net/forwarder.cpp
  src/net/forward_session.cpp
  src/net/socket_platform.cpp
)
target_include_directories(portforwardx_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(portforwardx_core
  PRIVATE
    PORTFORWARDX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PORTFORWARDX_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PORTFORWARDX_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
if(MSVC)
  target_compile_options(portforwardx_core PRIVATE /utf-8)
endif()
if(WIN32)
  target_link_libraries(portforwardx_core PRIVATE ws2_32)
endif()
portforwardx_apply_warnings(portforwardx_core)

if(PORTFORWARDX_BUILD_EXAMPLES)
  add_executable(portforwardx
    src/main.cpp
  )
  target_link_libraries(portforwardx
    PRIVATE
      portforwardx_core
      Boost::program_options
  )
  portforwardx_apply_warnings(portforwardx)
  if(MSVC)
    target_compile_options(portforwardx PRIVATE /utf-8)
  endif()
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT portforwardx)
endif()

if(PORTFORWARDX_BUILD_TESTS AND BUILD_TESTING)
  add_executable(portforwardx_local_udp_e2e_test
    tests/local_udp_e2e_test.cpp
  )
  target_link_libraries(portforwardx_local_udp_e2e_test PRIVATE portforwardx_core)
  if(WIN32)
    target_link_libraries(portforwardx_local_udp_e2e_test PRIVATE ws2_32)
  endif()
  portforwardx_apply_warnings(portforwardx_local_udp_e2e_test)
  add_test(NAME portforwardx_local_udp_e2e_test COMMAND portforwardx_local_udp_e2e_test)
  set_tests_properties(portforwardx_local_udp_e2e_test PROPERTIES TIMEOUT 60)

  add_executable(portforwardx_local_e2e_test
    tests/local_e2e_test.cpp
  )
  target_link_libraries(portforwardx_local_e2e_test PRIVATE portforwardx_core)
  if(WIN32)
    target_link_libraries(portforwardx_local_e2e_test PRIVATE ws2_32)
  endif()
  portforwardx_apply_warnings(portforwardx_local_e2e_test)
  add_test(NAME portforwardx_local_e2e_test COMMAND portforwardx_local_e2e_test)
  set_tests_properties(portforwardx_local_e2e_test PROPERTIES TIMEOUT 60)
endif()

install(TARGETS portforwardx_core
  EXPORT PortForwardXTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(PORTFORWARDX_BUILD_EXAMPLES)
  install(TARGETS portforwardx
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/manual.md DESTINATION . RENAME manual.md)
install(PROGRAMS
  ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_server.sh
  ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_client.sh
  ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_server.ps1
  ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_client.ps1
  ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_server.bat
  ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_client.bat
  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(EXPORT PortForwardXTargets
  FILE PortForwardXTargets.cmake
  NAMESPACE PortForwardX::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PortForwardX
)

if(PORTFORWARDX_BUILD_EXAMPLES AND PORTFORWARDX_INSTALL_RUNTIME_DEPS)
  set(PORTFORWARDX_INSTALL_RUNTIME_DEPS_SCRIPT
      "${CMAKE_CURRENT_BINARY_DIR}/cmake/InstallRuntimeDeps.cmake")
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallRuntimeDeps.cmake.in
    ${PORTFORWARDX_INSTALL_RUNTIME_DEPS_SCRIPT}
    @ONLY
  )
  install(SCRIPT ${PORTFORWARDX_INSTALL_RUNTIME_DEPS_SCRIPT})
endif()
