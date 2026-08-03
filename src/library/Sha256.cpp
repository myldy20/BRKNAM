// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Ilya Tolstoukhov (Myldy design / @myldy20)
// See NOTICE for the GPLv3 section 7(b) origin notice.

#include "Sha256.hpp"

#include "brknam/library/LibraryDatabase.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace brknam::library::detail {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

class Sha256 final {
 public:
  void update(const unsigned char* data, const std::size_t size) {
    total_bytes_ += static_cast<std::uint64_t>(size);
    std::size_t offset = 0;

    if (buffer_size_ != 0) {
      const auto required = buffer_.size() - buffer_size_;
      const auto copied = std::min(required, size);
      std::copy_n(data, copied,
                  buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_));
      buffer_size_ += copied;
      offset += copied;
      if (buffer_size_ == buffer_.size()) {
        transform(buffer_.data());
        buffer_size_ = 0;
      }
    }

    while (offset + buffer_.size() <= size) {
      transform(data + offset);
      offset += buffer_.size();
    }

    if (offset < size) {
      buffer_size_ = size - offset;
      std::copy_n(data + offset, buffer_size_, buffer_.begin());
    }
  }

  [[nodiscard]] std::string finish() {
    const std::uint64_t bit_length = total_bytes_ * 8U;
    buffer_[buffer_size_++] = 0x80U;

    if (buffer_size_ > 56U) {
      std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_),
                buffer_.end(), 0U);
      transform(buffer_.data());
      buffer_size_ = 0;
    }

    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_),
              buffer_.begin() + 56, 0U);
    for (std::size_t index = 0; index < 8; ++index) {
      buffer_[63U - index] =
          static_cast<unsigned char>((bit_length >> (index * 8U)) & 0xffU);
    }
    transform(buffer_.data());

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto value : state_) {
      output << std::setw(8) << value;
    }
    return output.str();
  }

 private:
  void transform(const unsigned char* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
      const auto base = index * 4U;
      words[index] =
          (static_cast<std::uint32_t>(block[base]) << 24U) |
          (static_cast<std::uint32_t>(block[base + 1U]) << 16U) |
          (static_cast<std::uint32_t>(block[base + 2U]) << 8U) |
          static_cast<std::uint32_t>(block[base + 3U]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
      const auto s0 = std::rotr(words[index - 15U], 7) ^
                      std::rotr(words[index - 15U], 18) ^
                      (words[index - 15U] >> 3U);
      const auto s1 = std::rotr(words[index - 2U], 17) ^
                      std::rotr(words[index - 2U], 19) ^
                      (words[index - 2U] >> 10U);
      words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];

    for (std::size_t index = 0; index < words.size(); ++index) {
      const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const auto choice = (e & f) ^ (~e & g);
      const auto temp1 =
          h + sum1 + choice + kRoundConstants[index] + words[index];
      const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto temp2 = sum0 + majority;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_{
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::array<unsigned char, 64> buffer_{};
  std::size_t buffer_size_{};
  std::uint64_t total_bytes_{};
};

}  // namespace

std::string sha256_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw LibraryDatabaseError("Unable to open asset for hashing");
  }

  Sha256 hash;
  std::array<unsigned char, 64U * 1024U> buffer{};
  while (input) {
    input.read(reinterpret_cast<char*>(buffer.data()),
               static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0) {
      hash.update(buffer.data(), static_cast<std::size_t>(count));
    }
  }
  if (!input.eof()) {
    throw LibraryDatabaseError("Unable to read asset while hashing");
  }
  return hash.finish();
}

}  // namespace brknam::library::detail
