// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "RealtimeModelSlot.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace brknam::audio::detail {
namespace {

[[nodiscard]] std::uint64_t double_bits(const double value) noexcept {
  return std::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] double bits_double(const std::uint64_t value) noexcept {
  return std::bit_cast<double>(value);
}

[[nodiscard]] double optional_value(
    const std::optional<double>& value) noexcept {
  return value.value_or(std::numeric_limits<double>::quiet_NaN());
}

[[nodiscard]] std::optional<double> decoded_optional(
    const std::uint64_t bits) noexcept {
  const auto value = bits_double(bits);
  return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

[[nodiscard]] float db_to_gain(const float db) noexcept {
  return std::pow(10.0F, db / 20.0F);
}

}  // namespace

struct RealtimeModelSlot::Node {
  explicit Node(std::unique_ptr<MonoModel> value)
      : model(std::move(value)), info(model != nullptr ? model->info()
                                                      : ModelInfo{}) {}

  std::unique_ptr<MonoModel> model;
  ModelInfo info;
};

bool RealtimeModelSlot::RetiredQueue::push(Node* const node) noexcept {
  const auto write = write_index_.load(std::memory_order_relaxed);
  const auto next = (write + 1) % entries_.size();
  if (next == read_index_.load(std::memory_order_acquire)) {
    return false;
  }
  entries_[write] = node;
  write_index_.store(next, std::memory_order_release);
  return true;
}

RealtimeModelSlot::Node* RealtimeModelSlot::RetiredQueue::pop() noexcept {
  const auto read = read_index_.load(std::memory_order_relaxed);
  if (read == write_index_.load(std::memory_order_acquire)) {
    return nullptr;
  }
  auto* const node = entries_[read];
  entries_[read] = nullptr;
  read_index_.store((read + 1) % entries_.size(), std::memory_order_release);
  return node;
}

void RealtimeModelSlot::RetiredQueue::reset_indices() noexcept {
  write_index_.store(0, std::memory_order_relaxed);
  read_index_.store(0, std::memory_order_relaxed);
  entries_.fill(nullptr);
}

void RealtimeModelSlot::AtomicInfo::store(const ModelInfo& info) noexcept {
  expected_sample_rate_bits_.store(
      double_bits(info.expected_sample_rate_hz), std::memory_order_relaxed);
  loudness_bits_.store(double_bits(optional_value(info.loudness_db)),
                       std::memory_order_relaxed);
  input_level_bits_.store(double_bits(optional_value(info.input_level_dbu)),
                          std::memory_order_relaxed);
  output_level_bits_.store(double_bits(optional_value(info.output_level_dbu)),
                           std::memory_order_relaxed);
  latency_samples_.store(std::max(0, info.latency_samples),
                         std::memory_order_release);
}

ModelInfo RealtimeModelSlot::AtomicInfo::load() const noexcept {
  ModelInfo info;
  info.expected_sample_rate_hz = bits_double(
      expected_sample_rate_bits_.load(std::memory_order_relaxed));
  info.loudness_db = decoded_optional(
      loudness_bits_.load(std::memory_order_relaxed));
  info.input_level_dbu = decoded_optional(
      input_level_bits_.load(std::memory_order_relaxed));
  info.output_level_dbu = decoded_optional(
      output_level_bits_.load(std::memory_order_relaxed));
  info.latency_samples =
      latency_samples_.load(std::memory_order_acquire);
  return info;
}

RealtimeModelSlot::RealtimeModelSlot() {
  current_info_.store(ModelInfo{});
}

RealtimeModelSlot::~RealtimeModelSlot() {
  clear_non_realtime();
}

void RealtimeModelSlot::prepare(const std::size_t maximum_block_frames,
                                const std::size_t crossfade_frames) {
  if (maximum_block_frames == 0) {
    throw std::invalid_argument("Maximum block size must be positive");
  }
  if (crossfade_frames == 0) {
    throw std::invalid_argument("Crossfade length must be positive");
  }

  clear_non_realtime();
  current_output_.assign(maximum_block_frames, 0.0F);
  previous_output_.assign(maximum_block_frames, 0.0F);
  maximum_block_frames_ = maximum_block_frames;
  crossfade_frames_ = crossfade_frames;
  prepared_ = true;
}

void RealtimeModelSlot::publish(std::unique_ptr<MonoModel> model) {
  if (!prepared_) {
    throw std::logic_error(
        "Processor must be prepared before publishing a model");
  }

  auto* const candidate = new Node(std::move(model));
  auto* const replaced =
      pending_.exchange(candidate, std::memory_order_acq_rel);
  delete replaced;
}

std::size_t RealtimeModelSlot::collect_retired() noexcept {
  std::size_t collected = 0;
  while (auto* const node = retired_.pop()) {
    delete node;
    ++collected;
  }
  return collected;
}

void RealtimeModelSlot::process(const float* const input, float* const output,
                                const std::size_t frames,
                                const OutputMode output_mode) noexcept {
  if (!prepared_ || input == nullptr || output == nullptr ||
      frames > maximum_block_frames_) {
    if (output != nullptr) {
      std::fill_n(output, frames, 0.0F);
    }
    return;
  }
  if (frames == 0) {
    return;
  }

  flush_waiting_retirement();
  accept_pending_if_possible();

  process_node(current_, input, current_output_.data(), frames);
  const auto current_gain = normalization_gain(current_, output_mode);

  if (!fade_active_) {
    for (std::size_t frame = 0; frame < frames; ++frame) {
      output[frame] = current_output_[frame] * current_gain;
    }
    return;
  }

  process_node(fade_from_, input, previous_output_.data(), frames);
  const auto previous_gain = normalization_gain(fade_from_, output_mode);
  const auto denominator =
      crossfade_frames_ > 1 ? static_cast<float>(crossfade_frames_ - 1) : 1.0F;

  for (std::size_t frame = 0; frame < frames; ++frame) {
    const auto absolute_frame = crossfade_position_ + frame;
    const auto mix = crossfade_frames_ <= 1
                         ? 1.0F
                         : std::min(1.0F,
                                    static_cast<float>(absolute_frame) /
                                        denominator);
    const auto previous = previous_output_[frame] * previous_gain;
    const auto current = current_output_[frame] * current_gain;
    output[frame] = previous + (current - previous) * mix;
  }

  if (crossfade_position_ + frames >= crossfade_frames_) {
    finish_crossfade();
  } else {
    crossfade_position_ += frames;
  }
}

bool RealtimeModelSlot::has_pending_change() const noexcept {
  return pending_.load(std::memory_order_acquire) != nullptr;
}

bool RealtimeModelSlot::crossfade_active() const noexcept {
  return fade_active_snapshot_.load(std::memory_order_acquire);
}

int RealtimeModelSlot::latency_samples() const noexcept {
  return reported_latency_.load(std::memory_order_acquire);
}

ModelInfo RealtimeModelSlot::model_info() const noexcept {
  return current_info_.load();
}

void RealtimeModelSlot::clear_non_realtime() noexcept {
  delete pending_.exchange(nullptr, std::memory_order_acq_rel);
  delete current_;
  current_ = nullptr;
  delete fade_from_;
  fade_from_ = nullptr;
  delete retire_waiting_;
  retire_waiting_ = nullptr;
  static_cast<void>(collect_retired());
  retired_.reset_indices();

  current_output_.clear();
  previous_output_.clear();
  maximum_block_frames_ = 0;
  crossfade_frames_ = 1;
  crossfade_position_ = 0;
  fade_active_ = false;
  prepared_ = false;
  current_info_.store(ModelInfo{});
  reported_latency_.store(0, std::memory_order_release);
  fade_active_snapshot_.store(false, std::memory_order_release);
}

void RealtimeModelSlot::flush_waiting_retirement() noexcept {
  if (retire_waiting_ != nullptr && retired_.push(retire_waiting_)) {
    retire_waiting_ = nullptr;
  }
}

void RealtimeModelSlot::accept_pending_if_possible() noexcept {
  if (fade_active_ || retire_waiting_ != nullptr) {
    return;
  }

  auto* const candidate =
      pending_.exchange(nullptr, std::memory_order_acq_rel);
  if (candidate == nullptr) {
    return;
  }

  fade_from_ = current_;
  current_ = candidate;
  crossfade_position_ = 0;
  fade_active_ = true;
  current_info_.store(current_->info);
  reported_latency_.store(
      std::max(node_latency(fade_from_), node_latency(current_)),
      std::memory_order_release);
  fade_active_snapshot_.store(true, std::memory_order_release);
}

void RealtimeModelSlot::finish_crossfade() noexcept {
  retire(fade_from_);
  fade_from_ = nullptr;
  crossfade_position_ = 0;
  fade_active_ = false;
  reported_latency_.store(node_latency(current_), std::memory_order_release);
  fade_active_snapshot_.store(false, std::memory_order_release);
}

void RealtimeModelSlot::retire(Node* const node) noexcept {
  if (node == nullptr) {
    return;
  }
  if (!retired_.push(node)) {
    retire_waiting_ = node;
  }
}

void RealtimeModelSlot::process_node(Node* const node, const float* const input,
                                     float* const output,
                                     const std::size_t frames) noexcept {
  if (node == nullptr || node->model == nullptr) {
    std::copy_n(input, frames, output);
    return;
  }
  node->model->process(input, output, frames);
}

float RealtimeModelSlot::normalization_gain(
    const Node* const node, const OutputMode mode) noexcept {
  if (node == nullptr || node->model == nullptr ||
      mode != OutputMode::normalized || !node->info.loudness_db.has_value() ||
      !std::isfinite(*node->info.loudness_db)) {
    return 1.0F;
  }
  return db_to_gain(static_cast<float>(
      OneSlotProcessor::kNormalizationTargetDb - *node->info.loudness_db));
}

int RealtimeModelSlot::node_latency(const Node* const node) noexcept {
  return node != nullptr && node->model != nullptr
             ? std::max(0, node->info.latency_samples)
             : 0;
}

}  // namespace brknam::audio::detail
