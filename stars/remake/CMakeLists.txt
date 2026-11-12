cmake_minimum_required(VERSION 3.20)
project(stars LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(STARS_FETCH_DEPS "Download and build SDL3 from source instead of using an installed package" OFF)
# Development: build only some of the part files in src/scenes (e.g. -DSTARS_PARTS="intro;cloud");
# parts that are not built show a placeholder.
set(STARS_PARTS "" CACHE STRING "part sources (basenames in src/scenes) to build; empty = all")
include(FetchContent)

# ---------------------------------------------------------------- SDL3
if(NOT STARS_FETCH_DEPS)
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
# src/*.cpp: the framework, STARS.EXE and the music; src/stars/*.cpp: the core library shared by the parts (the 3D
# engine, the fades, the tables); src/scenes/*.cpp: one port per part.
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB APP_SOURCES CONFIGURE_DEPENDS ${SRC}/*.cpp)
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${SRC}/stars/*.cpp)
if(STARS_PARTS STREQUAL "")
  file(GLOB SCENE_SOURCES CONFIGURE_DEPENDS ${SRC}/scenes/*.cpp)
else()
  set(SCENE_SOURCES "")
  foreach(p IN LISTS STARS_PARTS)
    if(EXISTS ${SRC}/scenes/${p}.cpp)
      list(APPEND SCENE_SOURCES ${SRC}/scenes/${p}.cpp)
    else()
      message(STATUS "STARS_PARTS: no src/scenes/${p}.cpp")
    endif()
  endforeach()
endif()

if(MSVC)
  set(STARS_WARNINGS /W3 /utf-8)
  set(STARS_DEFINES _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
  set(STARS_WARNINGS -Wall -Wno-unused-function -Wno-missing-field-initializers)
  set(STARS_DEFINES "")
endif()

add_executable(stars WIN32 ${APP_SOURCES} ${CORE_SOURCES} ${SCENE_SOURCES})
# "stars-remake", not "stars": on Windows stars.exe would overwrite the original STARS.EXE next to it.
set_target_properties(stars PROPERTIES OUTPUT_NAME stars-remake)
target_include_directories(stars PRIVATE src third_party)
target_link_libraries(stars PRIVATE SDL3::SDL3 Threads::Threads)
target_compile_options(stars PRIVATE ${STARS_WARNINGS})
target_compile_definitions(stars PRIVATE ${STARS_DEFINES})
if(MINGW)
  target_link_options(stars PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Sources whose floating point must be evaluated as written (no contraction into FMAs), so that the results are the
# same with every compiler: the GF1 mix and anything a part wants to keep bit-exact (add it here).
set(STARS_EXACT_FP_SOURCES ${SRC}/music.cpp ${SRC}/lcaplay.cpp ${SRC}/gf1.cpp)
foreach(f IN LISTS STARS_EXACT_FP_SOURCES)
  if(EXISTS ${f})
    set_source_files_properties(${f} PROPERTIES
      COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/fp:precise,-ffp-contract=off>")
  endif()
endforeach()

# tools/test_music: checks the soundtrack against the capture and the Python model (not built by default)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_music.cpp)
  set(TEST_MUSIC_SOURCES tools/test_music.cpp ${SRC}/exe.cpp ${SRC}/unpack.cpp)
  foreach(f music.cpp lcaplay.cpp gf1.cpp)
    if(EXISTS ${SRC}/${f})
      list(APPEND TEST_MUSIC_SOURCES ${SRC}/${f})
    endif()
  endforeach()
  add_executable(test_music EXCLUDE_FROM_ALL ${TEST_MUSIC_SOURCES})
  target_include_directories(test_music PRIVATE src)
  target_link_libraries(test_music PRIVATE Threads::Threads)
  target_compile_options(test_music PRIVATE ${STARS_WARNINGS})
  target_compile_definitions(test_music PRIVATE ${STARS_DEFINES})
endif()

# ---- music (src/music.*, src/lcaplay.*, src/gf1.*, tools/test_music.cpp) -------------------------------------
# test_music finds the capture (stars/re: unpacked/, full.gus, full.dram, full.ticks) from any working directory.
if(TARGET test_music)
  target_compile_definitions(test_music PRIVATE STARS_RE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../re")
endif()
