cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

include(FetchContent)

project(plib)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -EHa")

set(is_root_project OFF)	# indicate if this is the top-level project
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(is_root_project ON)
  message(STATUS "${PROJECT_NAME} is top level")
endif()

option(PLIB_ENABLE_TESTS "Enable building tests" ${is_root_project})

if(PLIB_ENABLE_TESTS)
  add_subdirectory(tests)
endif()



add_library(plib INTERFACE)
# target_sources(plib PRIVATE)
target_include_directories(plib INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
