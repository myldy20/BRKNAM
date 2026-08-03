// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#pragma once

#include "brknam/audio/OneSlotProcessor.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace brknam::audio::detail {

class RealtimeModelSlot final {
 public:
  static constexpr std::size_t kRetiredQueueCapacity = 16;

  RealtimeModelSlot();
  ~RealtimeModelSlot();

  RealtimeModelSlot(const RealtimeModelSlot&) = delete;
  RealtimeModelSlot& operator=(const RealtimeModelSlot&) = delete;

  // Non-realtime. Audio processing must be stopped. Destroys all current,
  // pending and retired models on the caller thread and reallocates scratch.
  void prepare(std::size_t maximum_block_frames,
               std::size_t crossfade_frames);

  // Non-realtime publisher. Ownership is transferred atomically. Passing a
  // null model requests a crossfade to dry. A not-yet-consumed request is
  // coalesced and destroyed on the publisher thread.
  void publish(std::unique_ptr<MonoModel> model);

  // Non-realtime single consumer. Deletes models that the audio thread has
  // finished using and returns the number collected.
  [[nodiscard]] std::size_t collect_retired() noexcept;

  // Realtime single consumer. No allocation, locking, deletion or I/O.
  void process(const float* input, float* output, std::size_t frames,
               OutputMode output_mode) noexcept;

  [[nodiscard]] bool has_pending_change() const noexcept;
  [[nodiscard]] bool crossfade_active() const noexcept;
  [[nodiscard]] int latency_samples() const noexcept;
  [[nodiscard]] ModelInfo model_info() const noexcept;

 private:
  struct Node;

  class RetiredQueue final {
   public:
    [[nodiscard]] bool push(Node* node) noexcept;
    [[nodiscard]] Node* pop() noexcept;
    void reset_indices() noexcept;

   private:
    // One slot is intentionally unused to distinguish full from empty.
    std::array<Node*, kRetiredQueueCapacity + 1> entries_{};
    std::atomic<std::size_t> write_index_{0};
    std::atomic<std::size_t> read_index_{0};
  };

  struct AtomicInfo final {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "BRKNAM realtime metadata requires lock-free 64-bit atomics");

    void store(const ModelInfo& info) noexcept;
    [[nodiscard]] ModelInfo load() const noexcept;

    std::atomic<std::uint64_t> expected_sample_rate_bits_{};
    std::atomic<std::uint64_t> loudness_bits_{};
    std::atomic<std::uint64_t> input_level_bits_{};
    std::atomic<std::uint64_t> output_level_bits_{};
    std::atomic<int> latency_samples_{0};
  };

  void clear_non_realtime() noexcept;
  void flush_waiting_retirement() noexcept;
  void accept_pending_if_possible() noexcept;
  void finish_crossfade() noexcept;
  void retire(Node* node) noexcept;
  void process_node(Node* node, const float* input, float* output,
                    std::size_t frames) noexcept;
  [[nodiscard]] static float normalization_gain(
      const Node* node, OutputMode mode) noexcept;
  [[nodiscard]] static int node_latency(const Node* node) noexcept;

  std::atomic<Node*> pending_{nullptr};
  Node* current_{};
  Node* fade_from_{};
  Node* retire_waiting_{};
  RetiredQueue retired_;

  std::vector<float> current_output_;
  std::vector<float> previous_output_;
  std::size_t maximum_block_frames_{};
  std::size_t crossfade_frames_{1};
  std::size_t crossfade_position_{};
  bool fade_active_{};
  bool prepared_{};

  AtomicInfo current_info_;
  std::atomic<int> reported_latency_{0};
  std::atomic<bool> fade_active_snapshot_{false};
};

}  // namespace brknam::audio::detail
