cmake_minimum_required(VERSION 3.20)
project(dope LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(DOPE_FETCH_DEPS "Download and build SDL3 from source instead of using an installed package" OFF)
# Development: build only some of the part files in src/scenes (e.g. -DDOPE_PARTS="p3;p5");
# parts that are not built show a placeholder.
set(DOPE_PARTS "" CACHE STRING "part sources (basenames in src/scenes) to build; empty = all")
include(FetchContent)

# ---------------------------------------------------------------- SDL3
if(NOT DOPE_FETCH_DEPS)
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
# src/*.cpp: the framework and DOPE.EXE's reader (DOPE_FRAMEWORK) and the soundtrack (everything else: music.cpp, the
# player and the GF1 model, written by the music port); src/dope/*.cpp: the core library shared by the parts (the 3D
# engine, labels, palettes, fades); src/scenes/*.cpp: one port per part.
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(DOPE_FRAMEWORK main.cpp demo.cpp render.cpp gl.cpp pictures.cpp player.cpp timeline.cpp exe.cpp rnc.cpp)
file(GLOB SRC_FILES CONFIGURE_DEPENDS ${SRC}/*.cpp)
set(APP_SOURCES "")
set(MUSIC_SOURCES "")
foreach(f IN LISTS SRC_FILES)
  get_filename_component(n ${f} NAME)
  if(n IN_LIST DOPE_FRAMEWORK)
    list(APPEND APP_SOURCES ${f})
  else()
    list(APPEND MUSIC_SOURCES ${f})
  endif()
endforeach()
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${SRC}/dope/*.cpp)
if(DOPE_PARTS STREQUAL "")
  file(GLOB SCENE_SOURCES CONFIGURE_DEPENDS ${SRC}/scenes/*.cpp)
else()
  set(SCENE_SOURCES "")
  foreach(p IN LISTS DOPE_PARTS)
    if(EXISTS ${SRC}/scenes/${p}.cpp)
      list(APPEND SCENE_SOURCES ${SRC}/scenes/${p}.cpp)
    else()
      message(STATUS "DOPE_PARTS: no src/scenes/${p}.cpp")
    endif()
  endforeach()
endif()

set(DOPE_INCLUDE_DIRS src third_party)

if(MSVC)
  set(DOPE_WARNINGS /W3 /utf-8)
  set(DOPE_DEFINES _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
  set(DOPE_WARNINGS -Wall -Wno-unused-function -Wno-missing-field-initializers)
  set(DOPE_DEFINES "")
endif()

# Development: extra source files from anywhere (e.g. an experiment outside the tree), added to the program.
set(DOPE_EXTRA_SOURCES "" CACHE STRING "extra source files to build into the program (development)")

add_executable(dope WIN32 ${APP_SOURCES} ${MUSIC_SOURCES} ${CORE_SOURCES} ${SCENE_SOURCES} ${DOPE_EXTRA_SOURCES})
# "dope-remake", not "dope": on Windows dope.exe would overwrite the original DOPE.EXE next to it.
set_target_properties(dope PROPERTIES OUTPUT_NAME dope-remake)
target_include_directories(dope PRIVATE ${DOPE_INCLUDE_DIRS})
target_link_libraries(dope PRIVATE SDL3::SDL3 Threads::Threads)
target_compile_options(dope PRIVATE ${DOPE_WARNINGS})
target_compile_definitions(dope PRIVATE ${DOPE_DEFINES})
if(MINGW)
  target_link_options(dope PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# The music sources' floating point is evaluated as written (no contraction into FMAs), so that the soundtrack is
# the same with every compiler. Add a part's file here if it must be bit-exact too.
foreach(f IN LISTS MUSIC_SOURCES)
  set_source_files_properties(${f} PROPERTIES
    COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/fp:precise,-ffp-contract=off>")
endforeach()

# tools/test_music: checks the soundtrack against the capture (not built by default; written by the music port)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_music.cpp)
  add_executable(test_music EXCLUDE_FROM_ALL tools/test_music.cpp ${SRC}/exe.cpp ${SRC}/rnc.cpp ${MUSIC_SOURCES})
  target_include_directories(test_music PRIVATE ${DOPE_INCLUDE_DIRS})
  target_link_libraries(test_music PRIVATE Threads::Threads)
  target_compile_options(test_music PRIVATE ${DOPE_WARNINGS})
  # test_music finds the capture (dope/re: unpacked/, full.gus, full.dram, full.ticks) from any working directory
  target_compile_definitions(test_music PRIVATE ${DOPE_DEFINES} DOPE_RE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../re")
endif()
