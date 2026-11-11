cmake_minimum_required(VERSION 3.20)
project(jizz LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(JIZZ_FETCH_DEPS "Download and build SDL3 from source instead of using an installed package" OFF)
# Development: build only some of the part files in src/scenes (e.g. -DJIZZ_PARTS="tbl;logo");
# parts that are not built show a placeholder colour.
set(JIZZ_PARTS "" CACHE STRING "part sources (basenames in src/scenes) to build; empty = all")
include(FetchContent)

# ---------------------------------------------------------------- SDL3
if(NOT JIZZ_FETCH_DEPS)
  find_package(SDL3 CONFIG QUIET)
endif()
if(NOT TARGET SDL3::SDL3)
  message(STATUS "SDL3: building from source")
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.24/SDL3-3.2.24.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(SDL3)
endif()

# ---------------------------------------------------------------- sources
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${SRC}/jizz/*.cpp)
if(JIZZ_PARTS STREQUAL "")
  file(GLOB SCENE_SOURCES CONFIGURE_DEPENDS ${SRC}/scenes/*.cpp)
else()
  set(SCENE_SOURCES "")
  foreach(p IN LISTS JIZZ_PARTS)
    if(EXISTS ${SRC}/scenes/${p}.cpp)
      list(APPEND SCENE_SOURCES ${SRC}/scenes/${p}.cpp)
    else()
      message(STATUS "JIZZ_PARTS: no src/scenes/${p}.cpp")
    endif()
  endforeach()
endif()
add_executable(jizz WIN32
  ${SRC}/main.cpp
  ${SRC}/exe.cpp
  ${SRC}/adam.cpp
  ${SRC}/music.cpp
  ${SRC}/mxmplay.cpp
  ${SRC}/gl.cpp
  ${SRC}/render.cpp
  ${SRC}/demo.cpp
  ${SRC}/timeline.cpp
  ${CORE_SOURCES}
  ${SCENE_SOURCES})
# "jizz-remake", not "jizz": on Windows jizz.exe would overwrite the original JIZZ.EXE next to it.
set_target_properties(jizz PROPERTIES OUTPUT_NAME jizz-remake)
target_include_directories(jizz PRIVATE src third_party)
target_link_libraries(jizz PRIVATE SDL3::SDL3)
if(MSVC)
  target_compile_options(jizz PRIVATE /W3 /utf-8)
  target_compile_definitions(jizz PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
  target_compile_options(jizz PRIVATE -Wall -Wno-unused-function -Wno-missing-field-initializers)
endif()
if(MINGW)
  target_link_options(jizz PRIVATE -static -static-libgcc -static-libstdc++)
endif()
if(WIN32 AND TARGET SDL3::SDL3-shared)
  add_custom_command(TARGET jizz POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:jizz>)
endif()
