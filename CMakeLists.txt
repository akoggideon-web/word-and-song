cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(ENABLE_WHISPER "Build offline Whisper speech detection" ON)
set(WHISPER_CPP_SOURCE_DIR "" CACHE PATH "Local whisper.cpp source tree (downloaded automatically when empty)")
set(WHISPER_CPP_VERSION "v1.8.5" CACHE STRING "whisper.cpp release tag to download")

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Gui)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/bible.cpp
    src/reference-parser.cpp
    src/live-state.cpp
    src/lower-third-source.cpp
    src/songs.cpp
    src/dock.cpp
    src/listener.cpp
    src/matcher.cpp
)

if(ENABLE_WHISPER)
  # whisper.cpp is built as a static library and linked into the plugin.
  set(_saved_warn_as_error ${CMAKE_COMPILE_WARNING_AS_ERROR})
  set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
  set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
  set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
  set(GGML_BLAS OFF CACHE BOOL "" FORCE)
  set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
  if(APPLE)
    set(GGML_METAL ON CACHE BOOL "" FORCE)
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
  endif()
  if(WIN32 OR (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64"))
    # Any PC from roughly 2013 onwards supports these.
    set(GGML_AVX ON CACHE BOOL "" FORCE)
    set(GGML_AVX2 ON CACHE BOOL "" FORCE)
    set(GGML_FMA ON CACHE BOOL "" FORCE)
    set(GGML_F16C ON CACHE BOOL "" FORCE)
  endif()

  if(WHISPER_CPP_SOURCE_DIR)
    add_subdirectory("${WHISPER_CPP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/whisper.cpp" EXCLUDE_FROM_ALL)
  else()
    include(FetchContent)
    FetchContent_Declare(
      whisper
      URL "https://github.com/ggml-org/whisper.cpp/archive/refs/tags/${WHISPER_CPP_VERSION}.tar.gz"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(whisper)
  endif()
  set(CMAKE_COMPILE_WARNING_AS_ERROR ${_saved_warn_as_error})

  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE whisper)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_WHISPER=1)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/whisper-engine.cpp)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Standalone tests for the reference parser and lyrics matcher (no OBS needed).
option(BUILD_TESTS "Build parser tests" OFF)
if(BUILD_TESTS)
  add_executable(word-and-song-tests tests/parser-tests.cpp src/reference-parser.cpp src/matcher.cpp)
  target_include_directories(word-and-song-tests PRIVATE src)
  set_target_properties(word-and-song-tests PROPERTIES CXX_STANDARD 17)
endif()
