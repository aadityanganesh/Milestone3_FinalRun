#ifndef FAST_MEMBERSHIP_FILTER_UINT64_H
#define FAST_MEMBERSHIP_FILTER_UINT64_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

// A one-hash membership filter for uint64_t keys. It has no false negatives,
// stores no payload keys/values, and is intentionally cheaper than a full Bloom
// probe on the mixed workloads where most overlay/base miss checks are negative.
class FastMembershipFilterUint64 {
 public:
  void clear() {
    bits_.clear();
    mask_ = 0;
  }

  void init(size_t expected_elements, double bits_per_element) {
    if (expected_elements < 64) expected_elements = 64;
    if (bits_per_element < 1.0) bits_per_element = 1.0;

    size_t target_bits =
        static_cast<size_t>(static_cast<double>(expected_elements) *
                            bits_per_element);
    target_bits = std::max<size_t>(target_bits, 1024);

    size_t pow2_bits = 1;
    while (pow2_bits < target_bits) {
      pow2_bits <<= 1;
    }
    bits_.assign((pow2_bits + 63) >> 6, 0ULL);
    mask_ = pow2_bits - 1;
  }

  void add(uint64_t key) {
    if (mask_ == 0) return;
    const size_t bit = static_cast<size_t>(mix(key) & mask_);
    bits_[bit >> 6] |= 1ULL << (bit & 63);
  }

  bool maybe_contains(uint64_t key) const {
    if (mask_ == 0) return true;
    const size_t bit = static_cast<size_t>(mix(key) & mask_);
    return (bits_[bit >> 6] & (1ULL << (bit & 63))) != 0;
  }

  size_t size_in_bytes() const { return bits_.size() * sizeof(uint64_t); }

 private:
  static uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
  }

  std::vector<uint64_t> bits_;
  size_t mask_ = 0;
};

#endif  // FAST_MEMBERSHIP_FILTER_UINT64_H
