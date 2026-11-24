cmake_minimum_required(VERSION 3.20)
project(okean LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(OKEAN_FETCH_DEPS "Download and build SDL3 from source instead of using an installed package" OFF)
# Development: build only some of the part files in src/scenes (e.g. -DOKEAN_PARTS="title;objects");
# parts that are not built show a placeholder.
set(OKEAN_PARTS "" CACHE STRING "part sources (basenames in src/scenes) to build; empty = all")
include(FetchContent)

# ---------------------------------------------------------------- SDL3
if(NOT OKEAN_FETCH_DEPS)
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
# src/*.cpp: the framework, KORSO2.EXE and the music; src/okean/*.cpp: the core library shared by the parts (the
# noise, the glowing lines, the palette ramps, the font); src/scenes/*.cpp: one port per part.
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB APP_SOURCES CONFIGURE_DEPENDS ${SRC}/*.cpp)
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${SRC}/okean/*.cpp)
if(OKEAN_PARTS STREQUAL "")
  file(GLOB SCENE_SOURCES CONFIGURE_DEPENDS ${SRC}/scenes/*.cpp)
else()
  set(SCENE_SOURCES "")
  foreach(p IN LISTS OKEAN_PARTS)
    if(EXISTS ${SRC}/scenes/${p}.cpp)
      list(APPEND SCENE_SOURCES ${SRC}/scenes/${p}.cpp)
    else()
      message(STATUS "OKEAN_PARTS: no src/scenes/${p}.cpp")
    endif()
  endforeach()
endif()

# The soundtrack (src/music.*, src/modplay.*, src/gf1.*): part of APP_SOURCES.
set(MUSIC_SOURCES music.cpp modplay.cpp gf1.cpp)

if(MSVC)
  set(OKEAN_WARNINGS /W3 /utf-8)
  set(OKEAN_DEFINES _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
  set(OKEAN_WARNINGS -Wall -Wno-unused-function -Wno-missing-field-initializers)
  set(OKEAN_DEFINES "")
endif()

add_executable(okean WIN32 ${APP_SOURCES} ${CORE_SOURCES} ${SCENE_SOURCES})
# "okean-remake", like the other remakes' programs (the original is KORSO2.EXE).
set_target_properties(okean PROPERTIES OUTPUT_NAME okean-remake)
target_include_directories(okean PRIVATE src third_party)
target_link_libraries(okean PRIVATE SDL3::SDL3 Threads::Threads)
target_compile_options(okean PRIVATE ${OKEAN_WARNINGS})
target_compile_definitions(okean PRIVATE ${OKEAN_DEFINES})
if(MINGW)
  target_link_options(okean PRIVATE -static -static-libgcc -static-libstdc++)
endif()
# SDL3 built from source on Windows: its DLL next to the program
if(WIN32 AND TARGET SDL3::SDL3-shared)
  add_custom_command(TARGET okean POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:okean>)
endif()

# Sources whose floating point must be evaluated as written (no contraction into FMAs), so that the results are the
# same with every compiler: the module synthesis, the player and the GF1 mix, and anything a part wants to keep
# bit-exact (add it here).
set(OKEAN_EXACT_FP_SOURCES ${SRC}/music.cpp ${SRC}/modplay.cpp ${SRC}/gf1.cpp)
foreach(f IN LISTS OKEAN_EXACT_FP_SOURCES)
  if(EXISTS ${f})
    set_source_files_properties(${f} PROPERTIES
      COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/fp:precise,-ffp-contract=off>")
  endif()
endforeach()

# tools/test_music: checks the soundtrack against the capture and the Python model (not built by default)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_music.cpp)
  set(TEST_MUSIC_SOURCES tools/test_music.cpp ${SRC}/exe.cpp ${SRC}/rnc.cpp)
  foreach(f IN LISTS MUSIC_SOURCES)
    if(EXISTS ${SRC}/${f})
      list(APPEND TEST_MUSIC_SOURCES ${SRC}/${f})
    endif()
  endforeach()
  add_executable(test_music EXCLUDE_FROM_ALL ${TEST_MUSIC_SOURCES})
  target_include_directories(test_music PRIVATE src)
  target_link_libraries(test_music PRIVATE Threads::Threads)
  target_compile_options(test_music PRIVATE ${OKEAN_WARNINGS})
  target_compile_definitions(test_music PRIVATE ${OKEAN_DEFINES} OKEAN_RE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../re")
endif()
