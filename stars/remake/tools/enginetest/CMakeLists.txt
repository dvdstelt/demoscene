# Checker for the 3D engine (src/stars/engine*): the float transform against an integer port of the original's, and
# shots of test scenes (spike ball, wall, bee) drawn with the engine for tools/compare.py. A separate project: the
# framework (src/*.cpp without main.cpp), the core library and these files; the parts in src/scenes are not built.
#   cmake -S tools/enginetest -B build-enginetest -G Ninja && cmake --build build-enginetest
#   ./build-enginetest/enginetest transform
#   ./build-enginetest/enginetest --shots 102.6,111.2 --shotdir OUT
cmake_minimum_required(VERSION 3.20)
project(stars_enginetest LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
find_package(SDL3 CONFIG REQUIRED)
find_package(Threads REQUIRED)
set(R ${CMAKE_CURRENT_SOURCE_DIR}/../..)
file(GLOB APP_SOURCES CONFIGURE_DEPENDS ${R}/src/*.cpp)
list(FILTER APP_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${R}/src/stars/*.cpp)
add_executable(enginetest enginetest.cpp scenes.cpp ${APP_SOURCES} ${CORE_SOURCES})
target_include_directories(enginetest PRIVATE ${R}/src ${R}/third_party)
target_link_libraries(enginetest PRIVATE SDL3::SDL3 Threads::Threads)
target_compile_definitions(enginetest PRIVATE STARS_DIR="${R}/..")
if(MSVC)
  target_compile_options(enginetest PRIVATE /W3 /utf-8)
  target_compile_definitions(enginetest PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
  target_compile_options(enginetest PRIVATE -Wall -Wno-unused-function -Wno-missing-field-initializers)
endif()
