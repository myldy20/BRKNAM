cmake_minimum_required(VERSION 3.24)

project(BRKNAM VERSION 0.1.0 LANGUAGES C CXX)

option(BRKNAM_BUILD_TOOLS "Build BRKNAM command-line tools" ON)
option(BRKNAM_BUILD_TESTS "Build BRKNAM tests" ON)
option(BRKNAM_USE_SYSTEM_SQLITE "Use a system SQLite library instead of the pinned amalgamation" OFF)
option(BRKNAM_ENABLE_NAM_CORE "Build the pinned NeuralAmpModelerCore adapter" OFF)

find_package(Threads REQUIRED)

if(BRKNAM_USE_SYSTEM_SQLITE)
  find_package(SQLite3 REQUIRED)
  add_library(brknam_sqlite INTERFACE)
  target_link_libraries(brknam_sqlite INTERFACE SQLite::SQLite3)
else()
  include(FetchContent)

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
  src/audio/ModelLoadWorker.cpp
  src/audio/OneSlotProcessor.cpp
  src/audio/RealtimeModelSlot.cpp
  src/library/LibraryDatabase.cpp
  src/library/LibraryDatabaseRefresh.cpp
  src/library/LibraryDatabaseSavedSearches.cpp
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
target_link_libraries(brknam_core PRIVATE BRKNAM::sqlite Threads::Threads)

if(MSVC)
  target_compile_options(brknam_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(brknam_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

if(BRKNAM_ENABLE_NAM_CORE)
  include(cmake/DependencyPins.cmake)
  include(FetchContent)

  FetchContent_Declare(
    brknam_nam_core_source
    GIT_REPOSITORY ${BRKNAM_NAM_CORE_REPOSITORY}
    GIT_TAG ${BRKNAM_NAM_CORE_COMMIT}
    GIT_SHALLOW FALSE
    GIT_PROGRESS TRUE
    GIT_SUBMODULES Dependencies/eigen
    GIT_SUBMODULES_RECURSE TRUE
    SOURCE_SUBDIR brknam-no-cmake-project
  )
  FetchContent_MakeAvailable(brknam_nam_core_source)

  file(GLOB BRKNAM_NAM_SOURCES_TOP CONFIGURE_DEPENDS
    "${brknam_nam_core_source_SOURCE_DIR}/NAM/*.cpp"
    "${brknam_nam_core_source_SOURCE_DIR}/NAM/*.c"
  )
  file(GLOB BRKNAM_NAM_SOURCES_SUB CONFIGURE_DEPENDS
    "${brknam_nam_core_source_SOURCE_DIR}/NAM/*/*.cpp"
  )

  add_library(brknam_nam_core STATIC
    ${BRKNAM_NAM_SOURCES_TOP}
    ${BRKNAM_NAM_SOURCES_SUB}
  )
  add_library(BRKNAM::nam_core ALIAS brknam_nam_core)
  target_compile_features(brknam_nam_core PUBLIC cxx_std_20)
  target_compile_definitions(brknam_nam_core
    PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST
  )
  target_include_directories(brknam_nam_core
    PUBLIC
      ${brknam_nam_core_source_SOURCE_DIR}
      ${brknam_nam_core_source_SOURCE_DIR}/Dependencies/eigen
      ${brknam_nam_core_source_SOURCE_DIR}/Dependencies/nlohmann
  )
  target_link_libraries(brknam_nam_core PRIVATE Threads::Threads)
  set_target_properties(brknam_nam_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
  )
  if(WIN32)
    target_compile_definitions(brknam_nam_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  endif()
  if(MSVC)
    target_compile_options(brknam_nam_core PRIVATE /bigobj)
  endif()

  add_library(brknam_nam_adapter
    src/audio/NamCoreModel.cpp
  )
  add_library(BRKNAM::nam_adapter ALIAS brknam_nam_adapter)
  target_compile_features(brknam_nam_adapter PUBLIC cxx_std_20)
  target_include_directories(brknam_nam_adapter PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )
  target_link_libraries(brknam_nam_adapter
    PUBLIC BRKNAM::core
    PRIVATE BRKNAM::nam_core
  )
  if(MSVC)
    target_compile_options(brknam_nam_adapter PRIVATE /W4 /permissive-)
  else()
    target_compile_options(brknam_nam_adapter PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    )
  endif()
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

  add_executable(brknam-json-tests tests/JsonOutputTests.cpp)
  target_link_libraries(brknam-json-tests PRIVATE BRKNAM::core)
  target_compile_features(brknam-json-tests PRIVATE cxx_std_20)
  add_test(NAME brknam-json-tests COMMAND brknam-json-tests)

  add_executable(brknam-audio-tests tests/OneSlotProcessorTests.cpp)
  target_link_libraries(brknam-audio-tests PRIVATE BRKNAM::core)
  target_compile_features(brknam-audio-tests PRIVATE cxx_std_20)
  add_test(NAME brknam-audio-tests COMMAND brknam-audio-tests)

  add_executable(brknam-model-loader-tests tests/ModelLoadWorkerTests.cpp)
  target_link_libraries(brknam-model-loader-tests PRIVATE BRKNAM::core)
  target_compile_features(brknam-model-loader-tests PRIVATE cxx_std_20)
  add_test(NAME brknam-model-loader-tests COMMAND brknam-model-loader-tests)
  set_tests_properties(brknam-model-loader-tests PROPERTIES TIMEOUT 20)

  if(BRKNAM_ENABLE_NAM_CORE)
    add_executable(brknam-nam-core-tests tests/NamCoreModelTests.cpp)
    target_link_libraries(brknam-nam-core-tests PRIVATE BRKNAM::nam_adapter)
    target_compile_features(brknam-nam-core-tests PRIVATE cxx_std_20)
    add_test(NAME brknam-nam-core-tests COMMAND brknam-nam-core-tests)
    set_tests_properties(brknam-nam-core-tests PROPERTIES LABELS nam-core)

    add_executable(brknam-architecture-reference-tests
      tests/NamArchitectureReferenceTests.cpp
    )
    target_link_libraries(brknam-architecture-reference-tests PRIVATE
      BRKNAM::nam_adapter
      BRKNAM::nam_core
    )
    target_compile_features(brknam-architecture-reference-tests PRIVATE cxx_std_20)
    add_test(NAME brknam-architecture-reference-tests
      COMMAND brknam-architecture-reference-tests
    )
    set_tests_properties(brknam-architecture-reference-tests PROPERTIES
      LABELS nam-core
      TIMEOUT 120
    )
  endif()

  add_executable(brknam-library-benchmark tests/LibrarySearchBenchmark.cpp)
  target_link_libraries(brknam-library-benchmark PRIVATE BRKNAM::core BRKNAM::sqlite)
  target_compile_features(brknam-library-benchmark PRIVATE cxx_std_20)
  add_test(NAME brknam-library-benchmark COMMAND brknam-library-benchmark)
  set_tests_properties(brknam-library-benchmark PROPERTIES
    LABELS performance
    TIMEOUT 90
  )
endif()
