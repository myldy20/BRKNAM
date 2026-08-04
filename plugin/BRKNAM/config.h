// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)

#pragma once

#define PLUG_NAME "BRKNAM"
#define PLUG_MFR "Myldy design"
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_VERSION_STR "0.1.0"
#define PLUG_UNIQUE_ID 'BrNm'
#define PLUG_MFR_ID 'Myld'
#define PLUG_URL_STR "https://github.com/myldy20/BRKNAM"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Ilya Tolstoukhov"
#define PLUG_CLASS_NAME BRKNAM

#define BUNDLE_NAME "BRKNAM"
#define BUNDLE_MFR "MyldyDesign"
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "BRKNAM"

// BRKNAM's Core is mono internally. One or two host inputs are averaged and
// the processed mono result is written to one or two host outputs.
#define PLUG_CHANNEL_IO "1-1 1-2 2-2"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 720
#define PLUG_HEIGHT 360
#define PLUG_FPS 30
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0

#define AUV2_ENTRY BRKNAM_Entry
#define AUV2_ENTRY_STR "BRKNAM_Entry"
#define AUV2_FACTORY BRKNAM_Factory
#define AUV2_VIEW_CLASS BRKNAM_View
#define AUV2_VIEW_CLASS_STR "BRKNAM_View"

#define VST3_SUBCATEGORY "Fx|Distortion"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
