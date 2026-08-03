// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#pragma once

#include <filesystem>
#include <string>

namespace brknam::library::detail {

[[nodiscard]] std::string sha256_file(const std::filesystem::path& path);

}  // namespace brknam::library::detail
