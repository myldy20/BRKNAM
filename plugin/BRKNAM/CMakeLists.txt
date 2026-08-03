# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)

iplug_add_plugin(BRKNAM
  SOURCES
    BRKNAM.cpp
    BRKNAM.h
    config.h
    resources/resource.h
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
