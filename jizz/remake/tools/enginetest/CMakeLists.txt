# Check tool for the vector engine (src/jizz/engine*): renders engine scenes from case files exported from
# render dumps of the original (jizz/re/models/engine/run_engine.py --dump-render) and compares them.
# A separate project: it links only the engine and the GL / exe helpers.
#   cmake -S tools/enginetest -B build-enginetest -G Ninja && cmake --build build-enginetest
cmake_minimum_required(VERSION 3.20)
project(jizz_enginetest LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
find_package(SDL3 CONFIG REQUIRED)
set(R ${CMAKE_CURRENT_SOURCE_DIR}/../..)
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS ${R}/src/jizz/engine*.cpp)
add_executable(enginetest enginetest.cpp ${R}/src/exe.cpp ${R}/src/adam.cpp ${R}/src/gl.cpp ${R}/src/render.cpp
  ${ENGINE_SOURCES})
target_include_directories(enginetest PRIVATE ${R}/src ${R}/third_party)
target_link_libraries(enginetest PRIVATE SDL3::SDL3)
if(NOT MSVC)
  target_compile_options(enginetest PRIVATE -Wall -Wno-unused-function -Wno-missing-field-initializers)
endif()
