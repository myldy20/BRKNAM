cmake_minimum_required(VERSION 3.24)

project(BRKNAM VERSION 0.1.0 LANGUAGES CXX)

option(BRKNAM_BUILD_TOOLS "Build BRKNAM command-line tools" ON)
option(BRKNAM_BUILD_TESTS "Build BRKNAM tests" ON)

add_library(brknam_core
  src/library/LibraryScanner.cpp
  src/nam/NamMetadataReader.cpp
)
add_library(BRKNAM::core ALIAS brknam_core)

target_include_directories(brknam_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(brknam_core PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(brknam_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(brknam_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

if(BRKNAM_BUILD_TOOLS)
  add_executable(brknam-scan tools/brknam_scan.cpp)
  target_link_libraries(brknam-scan PRIVATE BRKNAM::core)
  target_compile_features(brknam-scan PRIVATE cxx_std_20)

  add_executable(brknam-inspect tools/brknam_inspect.cpp)
  target_link_libraries(brknam-inspect PRIVATE BRKNAM::core)
  target_compile_features(brknam-inspect PRIVATE cxx_std_20)
endif()

if(BRKNAM_BUILD_TESTS)
  include(CTest)
  enable_testing()

  add_executable(brknam-tests tests/LibraryScannerTests.cpp)
  target_link_libraries(brknam-tests PRIVATE BRKNAM::core)
  target_compile_features(brknam-tests PRIVATE cxx_std_20)
  add_test(NAME brknam-tests COMMAND brknam-tests)

  add_executable(brknam-nam-tests tests/NamMetadataReaderTests.cpp)
  target_link_libraries(brknam-nam-tests PRIVATE BRKNAM::core)
  target_compile_features(brknam-nam-tests PRIVATE cxx_std_20)
  add_test(NAME brknam-nam-tests COMMAND brknam-nam-tests)
endif()
