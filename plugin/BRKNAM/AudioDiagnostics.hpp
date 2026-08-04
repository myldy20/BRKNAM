// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace brknam::ui {

inline void publish_peak(std::atomic<float>& destination,
                         const float candidate) noexcept {
  if (!std::isfinite(candidate) || candidate <= 0.0F) {
    return;
  }
  auto current = destination.load(std::memory_order_relaxed);
  while (candidate > current &&
         !destination.compare_exchange_weak(current, candidate,
                                            std::memory_order_relaxed,
                                            std::memory_order_relaxed)) {
  }
}

inline std::string format_peak_dbfs(const float peak) {
  if (!std::isfinite(peak) || peak <= 1.0e-6F) {
    return "-inf dBFS";
  }
  const auto db = 20.0F * std::log10(peak);
  char text[32]{};
  std::snprintf(text, sizeof(text), "%.1f dBFS", static_cast<double>(db));
  return text;
}

}  // namespace brknam::ui
