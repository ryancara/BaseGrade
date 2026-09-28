cmake_minimum_required(VERSION 3.16)
project(BaseGrade LANGUAGES CXX)

# On macOS choose architecture at configure time, e.g.
#   -DCMAKE_OSX_ARCHITECTURES=arm64
# or for a universal build:
#   -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Path to a checkout of https://github.com/AcademySoftwareFoundation/openfx
set(OFX_DIR "" CACHE PATH "OpenFX checkout (contains include/ and Support/)")
if(NOT OFX_DIR)
  message(FATAL_ERROR "Pass -DOFX_DIR=/path/to/openfx")
endif()

file(GLOB_RECURSE OFX_SUPPORT_SRC "${OFX_DIR}/Support/Library/*.cpp")

if(WIN32)
  set(OFX_ARCH "Win64")
  set(OFX_INSTALL "C:/Program Files/Common Files/OFX/Plugins")
elseif(APPLE)
  set(OFX_ARCH "MacOS")
  set(OFX_INSTALL "/Library/OFX/Plugins")
else()
  set(OFX_ARCH "Linux-x86-64")
  set(OFX_INSTALL "/usr/OFX/Plugins")
endif()

find_package(Threads REQUIRED)

add_library(BaseGrade MODULE plugin.cpp ${OFX_SUPPORT_SRC})
target_include_directories(BaseGrade PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${OFX_DIR}/include
  ${OFX_DIR}/Support/include)
target_link_libraries(BaseGrade PRIVATE Threads::Threads)

set(BUNDLE "${CMAKE_BINARY_DIR}/BaseGrade.ofx.bundle")
if(APPLE)
  file(MAKE_DIRECTORY "${BUNDLE}/Contents")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Info.plist.in"
    "${BUNDLE}/Contents/Info.plist"
    @ONLY)
endif()
set_target_properties(BaseGrade PROPERTIES
  PREFIX "" SUFFIX ".ofx"
  LIBRARY_OUTPUT_DIRECTORY "${BUNDLE}/Contents/${OFX_ARCH}"
  RUNTIME_OUTPUT_DIRECTORY "${BUNDLE}/Contents/${OFX_ARCH}")
if(NOT MSVC)
  target_compile_options(BaseGrade PRIVATE -O3)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(BaseGradeCoreTest tests/test_core.cpp)
  target_include_directories(BaseGradeCoreTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(BaseGradeCoreTest PRIVATE Threads::Threads)
  if(NOT MSVC)
    target_compile_options(BaseGradeCoreTest PRIVATE -O2)
  endif()
  add_test(NAME BaseGradeCoreTest COMMAND BaseGradeCoreTest)
endif()

install(DIRECTORY "${BUNDLE}" DESTINATION "${OFX_INSTALL}")
