cmake_minimum_required(VERSION 3.20)
project(inside LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(INSIDE_FETCH_DEPS "Download and build SDL3 from source instead of using an installed package" OFF)
# Development: build only some of the part files in src/scenes (e.g. -DINSIDE_PARTS="intro;logo");
# parts that are not built show a placeholder.
set(INSIDE_PARTS "" CACHE STRING "part sources (basenames in src/scenes) to build; empty = all")
include(FetchContent)

# ---------------------------------------------------------------- SDL3
if(NOT INSIDE_FETCH_DEPS)
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
find_package(Threads REQUIRED)

# ---------------------------------------------------------------- sources
# src/*.cpp: the framework, INSIDE.EXE / INSIDE.DAT and the music; src/inside/*.cpp: the core library shared by the
# parts (palette fades, indexed images and shade tables, the SurRender engine); src/scenes/*.cpp: one port per part.
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB APP_SOURCES CONFIGURE_DEPENDS ${SRC}/*.cpp)
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${SRC}/inside/*.cpp)
if(INSIDE_PARTS STREQUAL "")
  file(GLOB SCENE_SOURCES CONFIGURE_DEPENDS ${SRC}/scenes/*.cpp)
else()
  set(SCENE_SOURCES "")
  foreach(p IN LISTS INSIDE_PARTS)
    if(EXISTS ${SRC}/scenes/${p}.cpp)
      list(APPEND SCENE_SOURCES ${SRC}/scenes/${p}.cpp)
    else()
      message(STATUS "INSIDE_PARTS: no src/scenes/${p}.cpp")
    endif()
  endforeach()
endif()

# The soundtrack (src/music.*, src/amp.*, src/gusdrv.*, src/gf1.*): part of APP_SOURCES.
set(MUSIC_SOURCES music.cpp amp.cpp gusdrv.cpp gf1.cpp)

if(MSVC)
  set(INSIDE_WARNINGS /W3 /utf-8)
  set(INSIDE_DEFINES _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
  set(INSIDE_WARNINGS -Wall -Wno-unused-function -Wno-missing-field-initializers)
  set(INSIDE_DEFINES "")
endif()

add_executable(inside WIN32 ${APP_SOURCES} ${CORE_SOURCES} ${SCENE_SOURCES})
# "inside-remake", not "inside": on Windows inside.exe would overwrite the original INSIDE.EXE next to it.
set_target_properties(inside PROPERTIES OUTPUT_NAME inside-remake)
target_include_directories(inside PRIVATE src third_party)
target_link_libraries(inside PRIVATE SDL3::SDL3 Threads::Threads)
target_compile_options(inside PRIVATE ${INSIDE_WARNINGS})
target_compile_definitions(inside PRIVATE ${INSIDE_DEFINES})
if(MINGW)
  target_link_options(inside PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Sources whose floating point must be evaluated as written (no contraction into FMAs), so that the results are the
# same with every compiler: the module synthesis, the player and the GF1 mix, and anything a part wants to keep
# bit-exact (add it here).
set(INSIDE_EXACT_FP_SOURCES ${SRC}/music.cpp ${SRC}/amp.cpp ${SRC}/gusdrv.cpp ${SRC}/gf1.cpp
  ${SRC}/scenes/intro.cpp)
foreach(f IN LISTS INSIDE_EXACT_FP_SOURCES)
  if(EXISTS ${f})
    set_source_files_properties(${f} PROPERTIES
      COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/fp:precise,-ffp-contract=off>")
  endif()
endforeach()

# tools/test_music: checks the soundtrack against the capture and the Python model (not built by default)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_music.cpp)
  set(TEST_MUSIC_SOURCES tools/test_music.cpp ${SRC}/exe.cpp ${SRC}/unpack.cpp)
  foreach(f IN LISTS MUSIC_SOURCES)
    if(EXISTS ${SRC}/${f})
      list(APPEND TEST_MUSIC_SOURCES ${SRC}/${f})
    endif()
  endforeach()
  add_executable(test_music EXCLUDE_FROM_ALL ${TEST_MUSIC_SOURCES})
  target_include_directories(test_music PRIVATE src)
  target_link_libraries(test_music PRIVATE Threads::Threads)
  target_compile_options(test_music PRIVATE ${INSIDE_WARNINGS})
  target_compile_definitions(test_music PRIVATE ${INSIDE_DEFINES} INSIDE_RE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../re")
endif()
