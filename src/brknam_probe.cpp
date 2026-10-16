// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Myldy design
// A low-risk Max 9 Linux external smoke test: mono unity-gain passthrough.
#include "c74_min.h"

using namespace c74::min;

class brknam_probe : public object<brknam_probe>, public sample_operator<1, 1> {
 public:
  MIN_DESCRIPTION{"BRKNAM Push native Max external loader / DSP probe"};
  MIN_TAGS{"audio, diagnostics"};
  MIN_AUTHOR{"Myldy design"};

  inlet<> in{this, "(signal) input or (bang) diagnostic trigger", "signal"};
  outlet<> audio{this, "(signal) mono passthrough", "signal"};
  outlet<> status{this, "(anything) loader status"};

  message<> ping{this, "ping", "Report that the C++ module loaded", MIN_FUNCTION {
    status.send("ready");
    return {};
  }};

  sample operator()(sample x) { return x; }
};

MIN_EXTERNAL(brknam_probe);
