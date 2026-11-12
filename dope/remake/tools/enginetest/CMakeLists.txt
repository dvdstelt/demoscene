# Checker for the 3D engine (src/dope/engine*): the engine against frames of the bit-exact model
# (dope/re/models/engine, exported by export_model.py), and shots of test scenes of p2, p3, p5, p6, p7 drawn with the
# engine for tools/compare.py. A separate project: the framework (src/*.cpp without main.cpp and the player), the
# soundtrack (for the clock), the core library and these files; the parts in src/scenes are not built.
#   cmake -S tools/enginetest -B build-enginetest -G Ninja && cmake --build build-enginetest
#   ./build-enginetest/enginetest model OUT/p2_f07500.bin ... [--pngdir DIR]
#   ./build-enginetest/enginetest --shots 102.6,111.2 --shotdir OUT
cmake_minimum_required(VERSION 3.20)
project(dope_enginetest LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
find_package(SDL3 CONFIG REQUIRED)
find_package(Threads REQUIRED)
set(R ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(FRAMEWORK demo.cpp render.cpp gl.cpp pictures.cpp timeline.cpp exe.cpp rnc.cpp)
list(TRANSFORM FRAMEWORK PREPEND ${R}/src/)
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${R}/src/dope/*.cpp)
set(MUSIC music.cpp modplay.cpp gf1.cpp)
list(TRANSFORM MUSIC PREPEND ${R}/src/)
add_executable(enginetest enginetest.cpp scenes.cpp ${FRAMEWORK} ${MUSIC} ${CORE_SOURCES})
target_include_directories(enginetest PRIVATE ${R}/src ${R}/third_party)
target_link_libraries(enginetest PRIVATE SDL3::SDL3 Threads::Threads)
target_compile_definitions(enginetest PRIVATE DOPE_DIR="${R}/..")
if(MSVC)
  target_compile_options(enginetest PRIVATE /W3 /utf-8)
  target_compile_definitions(enginetest PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
  target_compile_options(enginetest PRIVATE -Wall -Wno-unused-function -Wno-missing-field-initializers)
endif()
