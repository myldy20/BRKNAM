cmake_minimum_required(VERSION 3.24)

project(BRKNAM VERSION 0.1.0 LANGUAGES C CXX)

option(BRKNAM_BUILD_TOOLS "Build BRKNAM command-line tools" ON)
option(BRKNAM_BUILD_TESTS "Build BRKNAM tests" ON)
option(BRKNAM_USE_SYSTEM_SQLITE "Use a system SQLite library instead of the pinned amalgamation" OFF)

if(BRKNAM_USE_SYSTEM_SQLITE)
  find_package(SQLite3 REQUIRED)
  add_library(brknam_sqlite INTERFACE)
  target_link_libraries(brknam_sqlite INTERFACE SQLite::SQLite3)
else()
  include(FetchContent)
  find_package(Threads REQUIRED)

  FetchContent_Declare(
    brknam_sqlite_source
    URL https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
    URL_HASH SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR brknam-no-cmake-project
  )
  FetchContent_MakeAvailable(brknam_sqlite_source)

  add_library(brknam_sqlite STATIC
    ${brknam_sqlite_source_SOURCE_DIR}/sqlite3.c
  )
  target_include_directories(brknam_sqlite PUBLIC
    ${brknam_sqlite_source_SOURCE_DIR}
  )
  target_compile_definitions(brknam_sqlite PRIVATE
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_ENABLE_FTS5=1
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_THREADSAFE=1
  )
  target_link_libraries(brknam_sqlite PRIVATE
    Threads::Threads
    ${CMAKE_DL_LIBS}
    $<$<PLATFORM_ID:Linux>:m>
  )
  set_target_properties(brknam_sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
add_library(BRKNAM::sqlite ALIAS brknam_sqlite)

add_library(brknam_core
  src/library/LibraryDatabase.cpp
  src/library/LibraryDatabaseRefresh.cpp
  src/library/LibraryDatabaseSupport.cpp
  src/library/LibraryScanner.cpp
  src/library/Sha256.cpp
  src/nam/NamMetadataReader.cpp
)
add_library(BRKNAM::core ALIAS brknam_core)

target_include_directories(brknam_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(brknam_core PUBLIC cxx_std_20)
target_link_libraries(brknam_core PRIVATE BRKNAM::sqlite)

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

  add_executable(brknam-library tools/brknam_library.cpp)
  target_link_libraries(brknam-library PRIVATE BRKNAM::core)
  target_compile_features(brknam-library PRIVATE cxx_std_20)
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

  add_executable(brknam-library-tests tests/LibraryDatabaseTests.cpp)
  target_link_libraries(brknam-library-tests PRIVATE BRKNAM::core BRKNAM::sqlite)
  target_compile_features(brknam-library-tests PRIVATE cxx_std_20)
  add_test(NAME brknam-library-tests COMMAND brknam-library-tests)
endif()
