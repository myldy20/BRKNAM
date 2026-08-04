# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)

# A source checkout of iPlug2 intentionally omits the Steinberg SDK. Populate
# the exact directory expected by iPlug2's VST3.cmake from the official pinned
# aggregate repository, including its official component submodules.
include(FetchContent)
set(BRKNAM_VST3_SDK_SOURCE_DIR
    "${IPLUG2_DIR}/Dependencies/IPlug/VST3_SDK")
FetchContent_Declare(
  brknam_vst3_sdk_source
  GIT_REPOSITORY ${BRKNAM_VST3_SDK_REPOSITORY}
  GIT_TAG ${BRKNAM_VST3_SDK_COMMIT}
  GIT_SHALLOW FALSE
  GIT_PROGRESS TRUE
  GIT_SUBMODULES_RECURSE TRUE
  SOURCE_DIR ${BRKNAM_VST3_SDK_SOURCE_DIR}
  SOURCE_SUBDIR brknam-no-cmake-project
)
FetchContent_MakeAvailable(brknam_vst3_sdk_source)

set(BRKNAM_UI_FONT
    "${IPLUG2_DIR}/Examples/IPlugEffect/resources/fonts/Roboto-Regular.ttf")
if(NOT EXISTS "${BRKNAM_UI_FONT}")
  message(FATAL_ERROR "Pinned iPlug2 UI font is missing: ${BRKNAM_UI_FONT}")
endif()

iplug_add_plugin(BRKNAM
  SOURCES
    BRKNAM.cpp
    BRKNAM.h
    config.h
    resources/resource.h
  RESOURCES
    ${BRKNAM_UI_FONT}
  FORMATS APP VST3 AU
  LINK BRKNAM::nam_adapter
  DEFINES SAMPLE_TYPE_FLOAT
  UI IGRAPHICS
)

foreach(target BRKNAM-app BRKNAM-vst3 BRKNAM-au)
  if(TARGET ${target})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
      target_compile_options(${target} PRIVATE /W4 /permissive- /bigobj)
    else()
      target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
      )
    endif()
  endif()
endforeach()
