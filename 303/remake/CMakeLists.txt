cmake_minimum_required(VERSION 3.20)
project(acme303 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(ACME303_FETCH_DEPS "Download and build SDL3 and libopenmpt from source instead of using installed packages" OFF)
include(FetchContent)

# ---------------------------------------------------------------- SDL3
if(NOT ACME303_FETCH_DEPS)
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

# ---------------------------------------------------------------- libopenmpt
set(OPENMPT_TARGET "")
if(NOT ACME303_FETCH_DEPS)
  find_package(libopenmpt CONFIG QUIET)          # vcpkg
  if(TARGET libopenmpt::libopenmpt)
    set(OPENMPT_TARGET libopenmpt::libopenmpt)
  else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(OPENMPT IMPORTED_TARGET libopenmpt)
      if(TARGET PkgConfig::OPENMPT)
        set(OPENMPT_TARGET PkgConfig::OPENMPT)
      endif()
    endif()
  endif()
endif()
if(OPENMPT_TARGET STREQUAL "")
  message(STATUS "libopenmpt: building from source")
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/libopenmpt.cmake)
  set(OPENMPT_TARGET openmpt_static)
endif()

# ---------------------------------------------------------------- demo
file(GLOB SCENE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/scenes/*.cpp)
add_executable(acme303 WIN32
  src/main.cpp
  src/assets.cpp
  src/audio.cpp
  src/gl.cpp
  src/render.cpp
  src/demo.cpp
  src/timeline.cpp
  src/fx.cpp
    src/exefx.cpp
  src/exe.cpp
  ${SCENE_SOURCES})
target_include_directories(acme303 PRIVATE src third_party)
target_link_libraries(acme303 PRIVATE SDL3::SDL3 ${OPENMPT_TARGET})
if(MSVC)
  target_compile_options(acme303 PRIVATE /W3 /utf-8)
  target_compile_definitions(acme303 PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(acme303 PRIVATE -Wall -Wno-unused-function -Wno-missing-field-initializers)
endif()
if(MINGW)
  target_link_options(acme303 PRIVATE -static -static-libgcc -static-libstdc++)
endif()
# ship the SDL3 runtime next to the executable on Windows
if(WIN32 AND TARGET SDL3::SDL3-shared)
  add_custom_command(TARGET acme303 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:acme303>)
endif()
