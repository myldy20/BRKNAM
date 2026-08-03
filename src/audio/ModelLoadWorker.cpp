// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "brknam/audio/ModelLoadWorker.hpp"

#include <condition_variable>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace brknam::audio {
namespace {

constexpr auto kRetirementPollInterval = std::chrono::milliseconds(10);

struct Request {
  std::uint64_t generation{};
  std::filesystem::path path;
  bool detach{};
};

}  // namespace

struct ModelLoadWorker::Impl {
  Impl(OneSlotProcessor& target_processor,
       const double target_sample_rate_hz,
       const std::size_t target_maximum_block_frames,
       Loader target_loader)
      : processor(target_processor),
        sample_rate_hz(target_sample_rate_hz),
        maximum_block_frames(target_maximum_block_frames),
        loader(std::move(target_loader)) {
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
      throw std::invalid_argument("Sample rate must be finite and positive");
    }
    if (maximum_block_frames == 0) {
      throw std::invalid_argument("Maximum block size must be positive");
    }
    if (!loader) {
      throw std::invalid_argument("Model loader callback is required");
    }

    thread = std::jthread([this] { run(); });
  }

  ~Impl() {
    {
      std::lock_guard lock(mutex);
      stopping = true;
    }
    wake.notify_all();
    if (thread.joinable()) {
      thread.join();
    }
  }

  [[nodiscard]] std::uint64_t request(std::filesystem::path path,
                                      const bool detach) {
    std::lock_guard lock(mutex);
    if (stopping || status.state == ModelLoadState::stopped) {
      throw std::logic_error("Model-load worker is stopping");
    }

    ++latest_generation;
    latest_request = Request{latest_generation, std::move(path), detach};
    status.state = ModelLoadState::queued;
    status.requested_generation = latest_generation;
    status.requested_path = latest_request.path;
    status.error_message.reset();
    wake.notify_all();
    return latest_generation;
  }

  [[nodiscard]] ModelLoadStatus snapshot() const {
    std::lock_guard lock(mutex);
    return status;
  }

  [[nodiscard]] bool wait_until_settled(
      const std::uint64_t generation,
      const std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex);
    return settled.wait_for(lock, timeout, [&] {
      return status.completed_generation >= generation ||
             status.state == ModelLoadState::stopped;
    });
  }

  void run() noexcept {
    std::uint64_t consumed_generation = 0;

    while (true) {
      Request task;
      {
        std::unique_lock lock(mutex);
        wake.wait_for(lock, kRetirementPollInterval, [&] {
          return stopping || latest_generation > consumed_generation;
        });
        if (stopping) {
          break;
        }
        if (latest_generation == consumed_generation) {
          lock.unlock();
          static_cast<void>(processor.collect_retired_models());
          continue;
        }

        task = latest_request;
        consumed_generation = task.generation;
        status.state = ModelLoadState::loading;
        status.requested_generation = task.generation;
        status.requested_path = task.path;
        status.error_message.reset();
      }

      std::unique_ptr<MonoModel> prepared_model;
      std::optional<std::string> error;
      if (!task.detach) {
        try {
          prepared_model = loader(task.path);
          if (prepared_model == nullptr) {
            throw std::runtime_error("Model loader returned no model");
          }
          prepared_model->prepare(sample_rate_hz, maximum_block_frames);
        } catch (const std::exception& exception) {
          error = exception.what();
        } catch (...) {
          error = "Unknown model-loading failure";
        }
      }

      {
        // Holding the request mutex across the short atomic publication closes
        // the last-request-wins race: a newer request cannot arrive between the
        // generation check and publication of an older prepared model.
        std::lock_guard lock(mutex);
        if (task.generation != latest_generation) {
          continue;
        }

        if (error.has_value()) {
          status.state = ModelLoadState::error;
          status.completed_generation = task.generation;
          status.error_message = std::move(error);
          settled.notify_all();
          continue;
        }

        try {
          processor.publish_prepared_model(std::move(prepared_model));
          status.state = ModelLoadState::published;
          status.completed_generation = task.generation;
          status.error_message.reset();
        } catch (const std::exception& exception) {
          status.state = ModelLoadState::error;
          status.completed_generation = task.generation;
          status.error_message = exception.what();
        } catch (...) {
          status.state = ModelLoadState::error;
          status.completed_generation = task.generation;
          status.error_message = "Unknown model-publication failure";
        }
        settled.notify_all();
      }

      static_cast<void>(processor.collect_retired_models());
    }

    static_cast<void>(processor.collect_retired_models());
    {
      std::lock_guard lock(mutex);
      status.state = ModelLoadState::stopped;
    }
    settled.notify_all();
  }

  OneSlotProcessor& processor;
  double sample_rate_hz{};
  std::size_t maximum_block_frames{};
  Loader loader;

  mutable std::mutex mutex;
  mutable std::condition_variable settled;
  std::condition_variable wake;
  std::jthread thread;
  bool stopping{};
  std::uint64_t latest_generation{};
  Request latest_request;
  ModelLoadStatus status;
};

ModelLoadWorker::ModelLoadWorker(OneSlotProcessor& processor,
                                 const double sample_rate_hz,
                                 const std::size_t maximum_block_frames,
                                 Loader loader)
    : impl_(std::make_unique<Impl>(processor, sample_rate_hz,
                                   maximum_block_frames,
                                   std::move(loader))) {}

ModelLoadWorker::~ModelLoadWorker() = default;

std::uint64_t ModelLoadWorker::request_model(
    const std::filesystem::path& model_path) {
  if (model_path.empty()) {
    throw std::invalid_argument("Model path must not be empty");
  }
  return impl_->request(model_path, false);
}

std::uint64_t ModelLoadWorker::request_detach() {
  return impl_->request({}, true);
}

ModelLoadStatus ModelLoadWorker::status() const {
  return impl_->snapshot();
}

bool ModelLoadWorker::wait_until_settled(
    const std::uint64_t generation,
    const std::chrono::milliseconds timeout) const {
  return impl_->wait_until_settled(generation, timeout);
}

const char* to_string(const ModelLoadState state) noexcept {
  switch (state) {
    case ModelLoadState::idle:
      return "idle";
    case ModelLoadState::queued:
      return "queued";
    case ModelLoadState::loading:
      return "loading";
    case ModelLoadState::published:
      return "published";
    case ModelLoadState::error:
      return "error";
    case ModelLoadState::stopped:
      return "stopped";
  }
  return "unknown";
}

}  // namespace brknam::audio
