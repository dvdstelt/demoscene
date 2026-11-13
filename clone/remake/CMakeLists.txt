cmake_minimum_required(VERSION 3.20)
project(clone LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(CLONE_FETCH_DEPS "Download and build SDL3 from source instead of using an installed package" OFF)
# Development: build only some of the part files in src/scenes (e.g. -DCLONE_PARTS="tube;zoom");
# parts that are not built show a placeholder.
set(CLONE_PARTS "" CACHE STRING "part sources (basenames in src/scenes) to build; empty = all")
include(FetchContent)

# ---------------------------------------------------------------- SDL3
if(NOT CLONE_FETCH_DEPS)
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
# src/*.cpp: the framework, CLONE.EXE and the music; src/clone/*.cpp: the core library shared by the parts (textures,
# random generator, the 3D engine); src/scenes/*.cpp: one port per part.
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB APP_SOURCES CONFIGURE_DEPENDS ${SRC}/*.cpp)
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${SRC}/clone/*.cpp)
if(CLONE_PARTS STREQUAL "")
  file(GLOB SCENE_SOURCES CONFIGURE_DEPENDS ${SRC}/scenes/*.cpp)
else()
  set(SCENE_SOURCES "")
  foreach(p IN LISTS CLONE_PARTS)
    if(EXISTS ${SRC}/scenes/${p}.cpp)
      list(APPEND SCENE_SOURCES ${SRC}/scenes/${p}.cpp)
    else()
      message(STATUS "CLONE_PARTS: no src/scenes/${p}.cpp")
    endif()
  endforeach()
endif()

# The soundtrack (src/music.*, src/mxmplay.*, src/gf1.*, src/modsynth.*): part of APP_SOURCES.
set(MUSIC_SOURCES music.cpp mxmplay.cpp gf1.cpp modsynth.cpp)

if(MSVC)
  set(CLONE_WARNINGS /W3 /utf-8)
  set(CLONE_DEFINES _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
  set(CLONE_WARNINGS -Wall -Wno-unused-function -Wno-missing-field-initializers)
  set(CLONE_DEFINES "")
endif()

add_executable(clone WIN32 ${APP_SOURCES} ${CORE_SOURCES} ${SCENE_SOURCES})
# "clone-remake", not "clone": on Windows clone.exe would overwrite the original CLONE.EXE next to it.
set_target_properties(clone PROPERTIES OUTPUT_NAME clone-remake)
target_include_directories(clone PRIVATE src third_party)
target_link_libraries(clone PRIVATE SDL3::SDL3 Threads::Threads)
target_compile_options(clone PRIVATE ${CLONE_WARNINGS})
target_compile_definitions(clone PRIVATE ${CLONE_DEFINES})
if(MINGW)
  target_link_options(clone PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Sources whose floating point must be evaluated as written (no contraction into FMAs), so that the results are the
# same with every compiler: the module synthesis, the player and the GF1 mix, and anything a part wants to keep
# bit-exact (add it here).
set(CLONE_EXACT_FP_SOURCES ${SRC}/music.cpp ${SRC}/mxmplay.cpp ${SRC}/gf1.cpp ${SRC}/modsynth.cpp)
foreach(f IN LISTS CLONE_EXACT_FP_SOURCES)
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
  target_compile_options(test_music PRIVATE ${CLONE_WARNINGS})
  target_compile_definitions(test_music PRIVATE ${CLONE_DEFINES} CLONE_RE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../re")
endif()
