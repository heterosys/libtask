cmake_minimum_required(VERSION 3.10)

if(POLICY CMP0076)
  cmake_policy(SET CMP0076 NEW)
endif()

if(POLICY CMP0111)
  cmake_policy(SET CMP0111 NEW)
endif()

project(task LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "-Wno-attributes")

find_package(Boost 1.59 COMPONENTS coroutine REQUIRED)
find_package(Boost 1.59 COMPONENTS stacktrace_basic)

set(TASK_SOURCE src/task.cpp)
add_library(task_objects OBJECT ${TASK_SOURCE})
set_property(TARGET task_objects PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(
  task_objects PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)

add_library(task_static STATIC $<TARGET_OBJECTS:task_objects>)
add_library(task_shared SHARED $<TARGET_OBJECTS:task_objects>)
add_library(task ALIAS task_static)
set(TASK_LINK_LIBRARIES
  glog pthread Boost::boost
  ${Boost_COROUTINE_LIBRARY} ${Boost_CONTEXT_LIBRARY})
target_link_libraries(
  task_static PRIVATE ${TASK_LINK_LIBRARIES})
target_include_directories(
  task_static PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_link_libraries(
  task_shared PRIVATE ${TASK_LINK_LIBRARIES})
target_include_directories(
  task_shared PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)

  list(APPEND CPACK_DEBIAN_PACKAGE_DEPENDS
       "libboost-coroutine-dev(>=${Boost_VERSION_STRING})")
  list(APPEND CPACK_RPM_PACKAGE_REQUIRES
       "boost${Boost_VERSION_MAJOR}${Boost_VERSION_MINOR}-coroutine")

if(Boost_STACKTRACE_BASIC_FOUND)
  message(STATUS "Building libtask with stacktrace")
  target_compile_definitions(task_static PRIVATE TASK_ENABLE_STACKTRACE=1)
  target_compile_definitions(task_shared PRIVATE TASK_ENABLE_STACKTRACE=1)
  target_link_libraries(
    task_static PRIVATE Boost::boost ${Boost_STACKTRACE_BASIC_LIBRARY} dl)
  target_link_libraries(
    task_shared PRIVATE Boost::boost ${Boost_STACKTRACE_BASIC_LIBRARY} dl)
  list(APPEND CPACK_DEBIAN_PACKAGE_DEPENDS
       "libboost-stacktrace-dev(>=${Boost_VERSION_STRING})")
  list(APPEND CPACK_RPM_PACKAGE_REQUIRES
       "boost${Boost_VERSION_MAJOR}${Boost_VERSION_MINOR}-stacktrace")
endif()

include(GNUInstallDirs)
install(
  TARGETS task_static task_shared
  EXPORT TaskTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(
  DIRECTORY "${CMAKE_SOURCE_DIR}/src/"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
  PATTERN "*.h")

set_target_properties(task_static PROPERTIES EXPORT_NAME task)
export(
  EXPORT TaskTargets
  NAMESPACE task::
  FILE ${CMAKE_CURRENT_BINARY_DIR}/task/TaskTargets.cmake)
install(
  EXPORT TaskTargets
  FILE TaskTargets.cmake
  NAMESPACE task::
  DESTINATION lib/cmake/task)

if(NOT DEFINED CPACK_PACKAGE_VERSION_MAJOR)
  set(CPACK_PACKAGE_VERSION_MAJOR 0)
endif()
if(NOT DEFINED CPACK_PACKAGE_VERSION_MINOR)
  set(CPACK_PACKAGE_VERSION_MINOR 0)
endif()
if(NOT DEFINED CPACK_PACKAGE_VERSION_PATCH)
  set(CPACK_PACKAGE_VERSION_PATCH git)
endif()
if(NOT DEFINED PACKAGE_VERSION)
  set(PACKAGE_VERSION
    "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
endif()

set(CPACK_PACKAGE_NAME libtask-dev)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++ task-parallel library")
set(CPACK_PACKAGE_VERSION ${PACKAGE_VERSION})

list(
  APPEND
  CPACK_DEBIAN_PACKAGE_DEPENDS
  "libgoogle-glog-dev" # required by libtask for logging messages
  "libboost-coroutine-dev(>=${Boost_VERSION_STRING})"
)
list(SORT CPACK_DEBIAN_PACKAGE_DEPENDS)
list(JOIN CPACK_DEBIAN_PACKAGE_DEPENDS ", " CPACK_DEBIAN_PACKAGE_DEPENDS)
list(
  APPEND
  CPACK_RPM_PACKAGE_REQUIRES
  "glog-devel" # required by libtask for logging messages
)
list(SORT CPACK_RPM_PACKAGE_REQUIRES)
list(JOIN CPACK_RPM_PACKAGE_REQUIRES ", " CPACK_RPM_PACKAGE_REQUIRES)

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Jason Lau")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

set(CPACK_GENERATOR DEB)
include(CPack)

enable_testing()
add_subdirectory(apps)
