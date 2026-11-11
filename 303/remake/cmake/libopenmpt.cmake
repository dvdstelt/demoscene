# Builds libopenmpt from its release source (used when no installed libopenmpt is found, e.g. on Windows).
include(FetchContent)
FetchContent_Declare(libopenmpt_src
  URL https://lib.openmpt.org/files/libopenmpt/src/libopenmpt-0.8.8+release.makefile.tar.gz
  URL_HASH SHA256=3c8c9357280cd5a16ae261871f3943e22ff6ccee06e4a70489f8457f03e714e5
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR no-cmake-here)
FetchContent_MakeAvailable(libopenmpt_src)
set(OMPT ${libopenmpt_src_SOURCE_DIR})
file(GLOB OMPT_SOURCES
  ${OMPT}/src/openmpt/all/*.cpp ${OMPT}/src/openmpt/base/*.cpp ${OMPT}/src/openmpt/logging/*.cpp
  ${OMPT}/src/openmpt/random/*.cpp ${OMPT}/common/*.cpp ${OMPT}/src/openmpt/fileformat_base/*.cpp
  ${OMPT}/src/openmpt/soundbase/*.cpp ${OMPT}/src/openmpt/soundfile_data/*.cpp ${OMPT}/soundlib/*.cpp
  ${OMPT}/soundlib/plugins/*.cpp ${OMPT}/soundlib/plugins/dmo/*.cpp ${OMPT}/sounddsp/*.cpp ${OMPT}/libopenmpt/*.cpp)
add_library(openmpt_static STATIC ${OMPT_SOURCES})
target_compile_features(openmpt_static PRIVATE cxx_std_17)
target_compile_definitions(openmpt_static PRIVATE LIBOPENMPT_BUILD)
target_include_directories(openmpt_static PRIVATE ${OMPT}/src ${OMPT}/common ${OMPT}/build/svn_version ${OMPT})
target_include_directories(openmpt_static PUBLIC ${OMPT})
if(MSVC)
  target_compile_options(openmpt_static PRIVATE /bigobj /W0 /utf-8)
  target_compile_definitions(openmpt_static PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(openmpt_static PRIVATE -w)
endif()
if(WIN32)
  target_link_libraries(openmpt_static PUBLIC rpcrt4 ole32)
endif()
