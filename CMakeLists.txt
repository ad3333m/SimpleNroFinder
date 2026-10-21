# Desktop build: used by CI to compile-check the shared code with the same
# flags as the Switch target.
cmake_minimum_required(VERSION 3.16)
project(simplenrofinder C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED sdl2 freetype2 libcurl libwebpdemux libwebp zlib)
find_package(Threads REQUIRED)

file(GLOB CORE_SOURCES
  source/util/*.cpp
  source/util/*.c
  source/net/*.cpp
  source/github/*.cpp
  source/store/*.cpp
  source/ui/*.cpp
  source/ui/*.c
  source/app/*.cpp)

add_executable(simplenrofinder
  source/main.cpp
  ${CORE_SOURCES}
  source/platform/desktop/platform_desktop.cpp
  third_party/yyjson.c)
target_include_directories(simplenrofinder PRIVATE source third_party ${DEPS_INCLUDE_DIRS})
target_compile_options(simplenrofinder PRIVATE ${DEPS_CFLAGS_OTHER} -Wall -Wno-unused-function)
target_link_directories(simplenrofinder PRIVATE ${DEPS_LIBRARY_DIRS})
target_link_libraries(simplenrofinder PRIVATE ${DEPS_LIBRARIES} Threads::Threads)
