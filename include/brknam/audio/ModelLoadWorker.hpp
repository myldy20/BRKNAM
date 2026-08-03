// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#pragma once

#include "brknam/audio/OneSlotProcessor.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace brknam::audio {

enum class ModelLoadState {
  idle,
  queued,
  loading,
  published,
  error,
  stopped,
};

struct ModelLoadStatus {
  ModelLoadState state{ModelLoadState::idle};
  std::uint64_t requested_generation{};
  std::uint64_t completed_generation{};
  std::filesystem::path requested_path;
  std::optional<std::string> error_message;
};

class ModelLoadWorker final {
 public:
  using Loader = std::function<std::unique_ptr<MonoModel>(
      const std::filesystem::path&)>;

  // The processor must outlive the worker and must already be prepared for the
  // supplied sample rate and maximum block size. The loader runs only on the
  // worker thread; it may perform file I/O, parsing and allocation.
  ModelLoadWorker(OneSlotProcessor& processor,
                  double sample_rate_hz,
                  std::size_t maximum_block_frames,
                  Loader loader);
  ~ModelLoadWorker();

  ModelLoadWorker(const ModelLoadWorker&) = delete;
  ModelLoadWorker& operator=(const ModelLoadWorker&) = delete;
  ModelLoadWorker(ModelLoadWorker&&) = delete;
  ModelLoadWorker& operator=(ModelLoadWorker&&) = delete;

  // Returns a monotonically increasing generation. Only the newest requested
  // generation may publish; completed obsolete loads are discarded by the
  // worker thread.
  [[nodiscard]] std::uint64_t request_model(
      const std::filesystem::path& model_path);

  // Requests a click-free transition to dry through the same publication path.
  [[nodiscard]] std::uint64_t request_detach();

  [[nodiscard]] ModelLoadStatus status() const;

  // Non-realtime convenience for tests, hosts and shutdown coordination.
  // Returns true when this generation or a newer generation has settled as
  // published/error. A superseded generation therefore cannot wait forever.
  [[nodiscard]] bool wait_until_settled(
      std::uint64_t generation,
      std::chrono::milliseconds timeout) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] const char* to_string(ModelLoadState state) noexcept;

}  // namespace brknam::audio
