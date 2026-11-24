# IW4Iris — Cinematic Studio for IW4x
#
# This CMake build covers the game-independent parts: the cinematic core
# library, its tests, and (for Windows targets) the launcher. The native IW4x
# component is built inside the IW4x client tree with IW4x's own premake
# build; see BUILD.md and engine/iw4x/.
cmake_minimum_required(VERSION 3.20)
project(IW4Iris VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IW4IRIS_BUILD_TESTS "Build the unit tests" ON)
option(IW4IRIS_BUILD_LAUNCHER "Build the Windows launcher" ${WIN32})
option(IW4IRIS_WERROR "Treat warnings as errors" ON)

set(IW4IRIS_JSON_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/single_include" CACHE PATH
    "nlohmann/json include directory")
if(NOT EXISTS "${IW4IRIS_JSON_INCLUDE}/nlohmann/json.hpp")
  message(FATAL_ERROR "nlohmann/json not found in ${IW4IRIS_JSON_INCLUDE}. Run: git submodule update --init")
endif()

function(iw4iris_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    if(IW4IRIS_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
      # GCC 12/13 middle-end false positives inside libstdc++ (std::string,
      # std::variant); GCC 14 no longer reports them.
      target_compile_options(${target} PRIVATE -Wno-stringop-overflow -Wno-array-bounds -Wno-maybe-uninitialized
                             -Wno-restrict -Wno-dangling-pointer)
    endif()
    if(IW4IRIS_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

add_library(iw4iris_core STATIC
  core/src/math.cpp
  core/src/curve.cpp
  core/src/camera.cpp
  core/src/timeline.cpp
  core/src/recording.cpp
  core/src/project.cpp
  core/src/rig.cpp
  core/src/exporters.cpp
  core/src/demo.cpp
  core/src/presets.cpp
)
target_include_directories(iw4iris_core PUBLIC core/include)
target_include_directories(iw4iris_core SYSTEM PUBLIC "${IW4IRIS_JSON_INCLUDE}")
iw4iris_warnings(iw4iris_core)

if(IW4IRIS_BUILD_TESTS)
  enable_testing()
  add_executable(iw4iris_tests
    tests/main.cpp
    tests/test_math.cpp
    tests/test_curve.cpp
    tests/test_camera.cpp
    tests/test_timeline.cpp
    tests/test_recording.cpp
    tests/test_project.cpp
    tests/test_history.cpp
    tests/test_rig.cpp
    tests/test_exporters.cpp
    tests/test_demo.cpp
    tests/test_presets.cpp
    tests/test_launcher.cpp
    launcher/src/InstallPlan.cpp
  )
  target_link_libraries(iw4iris_tests PRIVATE iw4iris_core)
  target_include_directories(iw4iris_tests PRIVATE launcher/src)
  iw4iris_warnings(iw4iris_tests)
  target_compile_definitions(iw4iris_tests PRIVATE IW4IRIS_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

  # The demo tests use IW4x's own Huffman coder (the one the game uses for
  # demo messages) straight from the pinned IW4x sources.
  set(IW4X_HUFFMAN "${CMAKE_CURRENT_SOURCE_DIR}/external/iw4x-client/src/Utils/Huffman.cpp")
  if(EXISTS "${IW4X_HUFFMAN}")
    add_library(iw4x_huffman STATIC "${IW4X_HUFFMAN}")
    target_include_directories(iw4x_huffman PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/external/iw4x-client/src/Utils")
    if(MSVC)
      target_compile_options(iw4x_huffman PRIVATE /FIarray /FIcstdint /FIcassert)
    else()
      target_compile_options(iw4x_huffman PRIVATE "SHELL:-include array" "SHELL:-include cstdint" "SHELL:-include cassert" -w)
    endif()
    target_link_libraries(iw4iris_tests PRIVATE iw4x_huffman)
    target_compile_definitions(iw4iris_tests PRIVATE IW4IRIS_HAVE_IW4X_HUFFMAN=1)
  else()
    message(WARNING "IW4x sources not found; demo tests run without the real Huffman coder")
  endif()

  add_test(NAME iw4iris_tests COMMAND iw4iris_tests)
endif()

if(IW4IRIS_BUILD_LAUNCHER)
  add_subdirectory(launcher)
endif()
