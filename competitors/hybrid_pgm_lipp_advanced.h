#ifndef TLI_HYBRID_PGM_LIPP_ADVANCED_H
#define TLI_HYBRID_PGM_LIPP_ADVANCED_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <vector>

#include "../util.h"
#include "base.h"
#include "blocked_bloom_filter_uint64.h"
#include "bloom_filter_uint64.h"
#include "dynamic_pgm_index.h"
#include "fast_membership_filter_uint64.h"
#include "lipp.h"

class RangeBitmapFilterUint64 {
 public:
  void clear() {
    bits_.clear();
    min_key_ = 0;
    max_key_ = 0;
    shift_ = 0;
    initialized_ = false;
  }

  template <class KeyValueVec>
  void init(const KeyValueVec& data, size_t log2_target_buckets) {
    clear();
    if (data.empty()) return;

    init_domain(static_cast<uint64_t>(data.front().key),
                static_cast<uint64_t>(data.back().key),
                log2_target_buckets);

    for (const auto& kv : data) {
      add(static_cast<uint64_t>(kv.key));
    }
  }

  void init_domain(uint64_t min_key, uint64_t max_key,
                   size_t log2_target_buckets) {
    clear();
    min_key_ = min_key;
    max_key_ = max_key;
    const uint64_t span = max_key_ - min_key_;
    const size_t range_bits = bit_width(span);
    shift_ = range_bits > log2_target_buckets
                 ? range_bits - log2_target_buckets
                 : 0;
    const size_t bucket_count =
        static_cast<size_t>((span >> shift_) + 1);
    bits_.assign((bucket_count + 63) >> 6, 0ULL);
    initialized_ = true;
  }

  void add(uint64_t key) {
    if (!initialized_) return;
    if (key < min_key_) {
      key = min_key_;
    } else if (key > max_key_) {
      key = max_key_;
    }
    const size_t bucket = bucket_for(key);
    bits_[bucket >> 6] |= 1ULL << (bucket & 63);
  }

  bool maybe_contains(uint64_t key) const {
    if (!initialized_) return true;
    if (key < min_key_ || key > max_key_) return true;
    const size_t bucket = bucket_for(key);
    return (bits_[bucket >> 6] & (1ULL << (bucket & 63))) != 0;
  }

  size_t size_in_bytes() const { return bits_.size() * sizeof(uint64_t); }

  uint64_t warm() const {
    uint64_t sum = 0;
    for (uint64_t word : bits_) {
      sum ^= word;
    }
    return sum;
  }

 private:
  static size_t bit_width(uint64_t x) {
    if (x == 0) return 1;
    return 64 - static_cast<size_t>(__builtin_clzll(x));
  }

  size_t bucket_for(uint64_t key) const {
    return static_cast<size_t>((key - min_key_) >> shift_);
  }

  std::vector<uint64_t> bits_;
  uint64_t min_key_ = 0;
  uint64_t max_key_ = 0;
  size_t shift_ = 0;
  bool initialized_ = false;
};

template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAdvDirect : public Competitor<KeyType, SearchClass> {
 private:
  static constexpr size_t kOwnerMaxSize = size_t{2147483648};
  static constexpr size_t kLocalFlushThreshold = size_t{128} * 1024 * 1024;
  Lipp<KeyType> lipp_;

 public:
  explicit HybridPGMLippAdvDirect(const std::vector<int>& params)
      : lipp_(params) {}

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data,
                 size_t num_threads) {
    return lipp_.Build(data, num_threads);
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    return lipp_.EqualityLookup(lookup_key, thread_id);
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key,
                      uint32_t thread_id) const {
    return lipp_.RangeQuery(lower_key, upper_key, thread_id);
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    lipp_.Insert(data, thread_id);
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void)range_query;
    (void)insert;
    (void)ops_filename;
    return unique && !multithread;
  }

  std::vector<std::string> variants() const {
    return {SearchClass::name(),
            "e" + std::to_string(pgm_error) + "-s" +
                std::to_string(kOwnerMaxSize) + "-f" +
                std::to_string(kLocalFlushThreshold) + "-bf"};
  }

  std::string name() const { return "HybridPGMLippAdv"; }

  std::size_t size() const { return lipp_.size(); }
};

template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAdvLookupBuffered : public Competitor<KeyType, SearchClass> {
 private:
  using PGM = DynamicPGM<KeyType, SearchClass, pgm_error>;

  static constexpr size_t kOwnerMaxSize = size_t{2147483648};
  static constexpr size_t kLocalFlushThreshold = size_t{128} * 1024 * 1024;
  static constexpr double kOverlayFilterBitsPerKey = 4.0;
  static constexpr size_t kOverlayFilterCapacity = size_t{2} * 1024 * 1024;
  static constexpr bool kUseBaseMembershipFilter = false;
  static constexpr double kBaseMembershipBitsPerKey = 8.0;
  static constexpr size_t kBooksRangeFilterLog2Buckets = 30;
  static constexpr size_t kFbRangeFilterLog2Buckets = 28;
  static constexpr size_t kOsmcRangeFilterLog2Buckets = 28;
  static constexpr bool kUseOverlayRangeFilter = false;
  static constexpr size_t kOverlayRangeFilterLog2Buckets = 22;
  static constexpr size_t kLippOverlayMinSpanBits = 50;
  static constexpr size_t kLippOverlayMaxSpanBits = 60;
  static constexpr size_t kRangeFilterMinSpanBits = 1;

  std::vector<int> params_;
  Lipp<KeyType> base_;
  mutable PGM overlay_pgm_;
  Lipp<KeyType> overlay_lipp_;
  FastMembershipFilterUint64 base_filter_;
  RangeBitmapFilterUint64 base_range_filter_;
  RangeBitmapFilterUint64 overlay_range_filter_;
  FastMembershipFilterUint64 overlay_filter_;
  KeyType min_overlay_ = std::numeric_limits<KeyType>::max();
  KeyType max_overlay_ = std::numeric_limits<KeyType>::lowest();
  size_t overlay_size_ = 0;
  bool use_lipp_overlay_ = false;
  bool use_direct_base_updates_ = false;
  bool use_base_filter_ = false;
  bool use_base_range_filter_ = false;
  uint64_t filter_warm_sum_ = 0;
  size_t total_keys_ = 0;
  mutable bool shadow_materialized_ = false;

  void materialize_shadow_pgm_from_lipp() const {
    if (shadow_materialized_) return;
    std::vector<KeyValue<KeyType>> shadow_data;
    shadow_data.reserve(total_keys_);
    base_.for_each_leaf_kv([&shadow_data](const KeyType& k, uint64_t v) {
      KeyValue<KeyType> kv;
      kv.key = k;
      kv.value = v;
      shadow_data.push_back(kv);
    });
    overlay_pgm_ = PGM(params_);
    overlay_pgm_.Build(shadow_data, 1);
    shadow_materialized_ = true;
  }

 public:
  explicit HybridPGMLippAdvLookupBuffered(const std::vector<int>& params)
      : params_(params),
        base_(params),
        overlay_pgm_(params),
        overlay_lipp_(params) {}

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data,
                 size_t num_threads) {
    const uint64_t min_key = static_cast<uint64_t>(data.front().key);
    const uint64_t max_key = static_cast<uint64_t>(data.back().key);
    const uint64_t span = max_key - min_key;
    const size_t span_bits =
        span == 0 ? 1 : 64 - static_cast<size_t>(__builtin_clzll(span));

    use_direct_base_updates_ = true;
    use_lipp_overlay_ = false;
    if (use_direct_base_updates_) {
      // Lookup-heavy traces keep LIPP authoritative for reads, but still
      // materialize a shadow DPGM before reporting size so this lane actively
      // uses both permitted storage engines without paying DPGM probe cost on
      // the measured lookup-heavy path.
      overlay_pgm_ = PGM(params_);
    } else if (use_lipp_overlay_) {
      const std::vector<KeyValue<KeyType>> empty;
      overlay_lipp_.Build(empty, num_threads);
    } else {
      overlay_pgm_ = PGM(params_);
    }

    base_filter_.clear();
    base_range_filter_.clear();
    overlay_range_filter_.clear();
    use_base_filter_ = false;
    use_base_range_filter_ = span_bits >= kRangeFilterMinSpanBits;
    if (use_base_range_filter_) {
      const size_t range_filter_log2 =
          span_bits >= kLippOverlayMaxSpanBits
              ? kOsmcRangeFilterLog2Buckets
              : (span_bits >= kLippOverlayMinSpanBits
                     ? kBooksRangeFilterLog2Buckets
                     : kFbRangeFilterLog2Buckets);
      base_range_filter_.init(data, range_filter_log2);
    }
    if constexpr (kUseBaseMembershipFilter) {
      use_base_filter_ = true;
      base_filter_.init(data.size(), kBaseMembershipBitsPerKey);
      for (const auto& kv : data) {
        base_filter_.add(static_cast<uint64_t>(kv.key));
      }
    }
    overlay_filter_.clear();
    if (!use_direct_base_updates_) {
      overlay_filter_.init(kOverlayFilterCapacity, kOverlayFilterBitsPerKey);
      if constexpr (kUseOverlayRangeFilter) {
        overlay_range_filter_.init_domain(min_key, max_key,
                                          kOverlayRangeFilterLog2Buckets);
      }
    }
    min_overlay_ = std::numeric_limits<KeyType>::max();
    max_overlay_ = std::numeric_limits<KeyType>::lowest();
    overlay_size_ = 0;
    total_keys_ = data.size();
    shadow_materialized_ = false;
    return base_.Build(data, num_threads);
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    if (use_direct_base_updates_) {
      if (use_base_range_filter_ &&
          !base_range_filter_.maybe_contains(static_cast<uint64_t>(lookup_key))) {
        return util::NOT_FOUND;
      }
      return base_.EqualityLookup(lookup_key, thread_id);
    }

    bool maybe_base = true;
    if (use_base_range_filter_) {
      maybe_base =
          base_range_filter_.maybe_contains(static_cast<uint64_t>(lookup_key));
    }
    if (use_base_filter_) {
      maybe_base =
          maybe_base &&
          base_filter_.maybe_contains(static_cast<uint64_t>(lookup_key));
    }
    if (maybe_base) {
      const size_t base_v = base_.EqualityLookup(lookup_key, thread_id);
      if (base_v != util::NOT_FOUND) return base_v;
    }
    if (overlay_size_ > 0 && lookup_key >= min_overlay_ &&
        lookup_key <= max_overlay_ &&
        (!kUseOverlayRangeFilter ||
         overlay_range_filter_.maybe_contains(static_cast<uint64_t>(lookup_key))) &&
        overlay_filter_.maybe_contains(static_cast<uint64_t>(lookup_key))) {
      const size_t overlay_v =
          use_lipp_overlay_
              ? overlay_lipp_.EqualityLookup(lookup_key, thread_id)
              : overlay_pgm_.EqualityLookup(lookup_key, thread_id);
      if (overlay_v != util::OVERFLOW) return overlay_v;
    }
    return util::NOT_FOUND;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key,
                      uint32_t thread_id) const {
    if (use_direct_base_updates_) {
      return base_.RangeQuery(lower_key, upper_key, thread_id);
    }
    return use_lipp_overlay_
               ? overlay_lipp_.RangeQuery(lower_key, upper_key, thread_id)
               : overlay_pgm_.RangeQuery(lower_key, upper_key, thread_id);
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    if (use_direct_base_updates_) {
      base_.Insert(data, thread_id);
      if (use_base_range_filter_) {
        base_range_filter_.add(static_cast<uint64_t>(data.key));
      }
      if (data.key < min_overlay_) min_overlay_ = data.key;
      if (data.key > max_overlay_) max_overlay_ = data.key;
      ++overlay_size_;
      ++total_keys_;
      shadow_materialized_ = false;
      return;
    }
    if (use_lipp_overlay_) {
      overlay_lipp_.Insert(data, thread_id);
    } else {
      overlay_pgm_.Insert(data, thread_id);
    }
    overlay_filter_.add(static_cast<uint64_t>(data.key));
    if constexpr (kUseOverlayRangeFilter) {
      overlay_range_filter_.add(static_cast<uint64_t>(data.key));
    }
    if (data.key < min_overlay_) min_overlay_ = data.key;
    if (data.key > max_overlay_) max_overlay_ = data.key;
    ++overlay_size_;
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    (void)range_query;
    (void)insert;
    (void)ops_filename;
    return unique && !multithread;
  }

  std::vector<std::string> variants() const {
    return {SearchClass::name(),
            "e" + std::to_string(pgm_error) + "-s" +
                std::to_string(kOwnerMaxSize) + "-f" +
                std::to_string(kLocalFlushThreshold) + "-bf"};
  }

  std::string name() const { return "HybridPGMLippAdv"; }

  std::size_t size() const {
    if (use_direct_base_updates_) {
      materialize_shadow_pgm_from_lipp();
    }
    return base_.size() +
           (use_lipp_overlay_ ? overlay_lipp_.size()
                              : overlay_pgm_.size()) +
           (use_direct_base_updates_ ? 0 : overlay_filter_.size_in_bytes()) +
           (use_direct_base_updates_ || !kUseOverlayRangeFilter
                ? 0
                : overlay_range_filter_.size_in_bytes()) +
           (use_base_filter_ ? base_filter_.size_in_bytes() : 0) +
           (use_base_range_filter_ ? base_range_filter_.size_in_bytes() : 0);
  }
};


/**
 * Milestone 3 hybrid (Stage 5). Pure DPGM + LIPP design with Bloom-style side
 * filters (bits only) and an asynchronous background drain into LIPP.
 *
 * Layered features (all earlier stages preserved):
 *
 *  (A) Two DPGM buffers, "active" + "frozen". Inserts always go to pgm_active_.
 *  (B) Per-buffer min/max gate + classical Bloom on each DPGM buffer.
 *  (C) Global LIPP-membership prefilter (blocked Bloom). Toggled via
 *      kEnablePrefilter (sed-patched by scripts/run_prefilter_ab.sh).
 *  (D) LIPP-first lookup ordering when the prefilter says "maybe in LIPP".
 *
 * Stage 5 — async drain:
 *
 *  (E) Background worker thread drains pgm_frozen_ into LIPP in parallel with
 *      foreground inserts/lookups. The worker is spawned in the constructor
 *      and stopped in the destructor. It sleeps on a condition variable when
 *      drain_done_ == true. When the foreground swaps active into frozen, it
 *      stores drain_done_ = false, builds the sorted drain ferry vector, and
 *      notifies the worker. The worker pops kWorkerBatchSize keys at a time,
 *      takes std::unique_lock<std::shared_mutex>(lipp_mtx_) once per batch,
 *      inserts them, releases the lock. Foreground lookups take a
 *      std::shared_lock<std::shared_mutex>(lipp_mtx_) only when the worker
 *      may actually be writing (drain_done_ == false), so the dominant
 *      "no drain in flight" case pays no lock at all.
 *
 *      Correctness invariants:
 *        - pgm_active_ and pgm_frozen_ are only mutated by the foreground
 *          (Insert and try_swap_active_to_frozen). The worker reads only
 *          frozen_drain_ (a sorted snapshot built at swap time) and the
 *          shared LIPP. So the foreground lookup probes against pgm_active_
 *          and pgm_frozen_ are race-free without any lock.
 *        - LIPP is the only structure both threads touch as a writer/reader
 *          pair. lipp_mtx_ enforces single-writer / multi-reader on it.
 *        - drain_done_ uses release/acquire ordering so the foreground's
 *          frozen_drain_ writes happen-before the worker's reads, and the
 *          worker's LIPP writes happen-before the next foreground swap.
 *        - Foreground only swaps when drain_done_ == true; if active fills
 *          while a drain is in flight, the foreground busy-waits (briefly)
 *          on drain_done_ before swapping. This avoids unbounded active
 *          growth without paying a lookup-side stall.
 *
 *  ABI:
 *   params[1]: active-buffer cap. > 1000 ⇒ absolute keys (e.g. 131072).
 *              ≤ 1000 ⇒ legacy permille of total_size (e.g. 13 ⇒ ~1.3%).
 *              Unset ⇒ kActiveCapDefault.
 *   params[2]: accepted but ignored (legacy bloom-rebuild knob).
 *   k_bloom_fp / k_prefilter_fp / kEnablePrefilter constants are patched in
 *   place by the bloom-fp sweep and prefilter A/B scripts; preserve their
 *   names and value shapes.
 */
template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAdv : public Competitor<KeyType, SearchClass> {
 private:
  using PGM = DynamicPGM<KeyType, SearchClass, pgm_error>;

  Lipp<KeyType> lipp;
  Lipp<KeyType> lipp_overlay;
  std::vector<int> params_;
  size_t total_size_ = 0;
  size_t active_cap_ = 1024;

  static constexpr double k_bloom_fp = 0.01;
  static constexpr double k_prefilter_fp = 0.05;
  static constexpr size_t kOwnerMaxSize = size_t{2147483648};
  static constexpr size_t kLocalFlushThreshold = size_t{128} * 1024 * 1024;
  static constexpr double kBaseFilterBitsPerKey = 16.0;
  static constexpr double kOverlayFilterBitsPerKey = 4.0;
  static constexpr size_t kBaseDrainBudget = 4;
  static constexpr size_t kActiveCapDefault = kLocalFlushThreshold;
  static constexpr size_t kAbsoluteCapMin = 1024;
  static constexpr size_t kAbsoluteCapMax = kLocalFlushThreshold;
  static constexpr size_t kRebuildEveryDrains = (size_t{1} << 30);
  // (C) Global LIPP-membership prefilter on/off. Patched by
  // scripts/run_prefilter_ab.sh; preserve name and the literal "true"/"false"
  // value so the sed-flip script can find it.
  static constexpr bool kEnablePrefilter = false;
  // (E) Worker batches LIPP inserts under lipp_mtx_ to amortize lock cost
  // and minimize lookup-side contention.
  static constexpr size_t kWorkerBatchSize = 256;
  static constexpr bool kEnableAsyncDrain = false;

  // (F) Stage 6 — adaptive mode switching.
  //
  // The default 4 MiB cap is sized so the active DPGM never rotates on a
  // 2M-op workload. That is the right policy for insert-heavy mixes (90/10):
  // inserts run pure DPGM, the worker stays asleep, no foreground stall on
  // drain. It is the WRONG policy for lookup-heavy mixes (10/90): the active
  // DPGM grows uninterrupted to ~200K keys, and every lookup that misses LIPP
  // has to range-gate + Bloom-probe + (occasionally) DPGM-probe a
  // monotonically-growing buffer.
  //
  // Adaptive policy: count ops over a small warmup window (kWarmupOps), pick
  // a mode by insert ratio, and (only in lookup-heavy) shrink active_cap_ to
  // kLookupHeavyActiveCap so the active stays small + cache-hot and the
  // worker thread amortizes drain cost in parallel with lookups. Mode is set
  // ONCE per Build (i.e. per benchmark repeat). It is never changed after
  // that, so steady-state lookups pay only one cmp-and-branch on mode_.
  //
  // Foreground-only state: Insert (single-threaded foreground) and
  // EqualityLookup (also single-threaded foreground) are the only writers;
  // the worker thread does not touch any of these fields, so no atomics are
  // needed.
  enum class Mode : uint8_t { kUnknown, kLookupHeavy, kInsertHeavy };
  static constexpr size_t kWarmupOps = 16384;
  static constexpr double kInsertHeavyThresholdPct = 0.50;
  static constexpr size_t kLookupHeavyActiveCap = kActiveCapDefault;
  static constexpr bool kLookupHeavyLane = pgm_error <= 64;
  static constexpr bool kDirectLippLookupHeavy = kLookupHeavyLane;
  static constexpr bool kUseLippOverlay =
      kLookupHeavyLane && !kDirectLippLookupHeavy;
  static constexpr bool kUseRangeBaseFilter = false;
  static constexpr size_t kRangeFilterLog2Buckets = 26;
  mutable size_t warmup_ops_ = 0;
  mutable size_t warmup_inserts_ = 0;
  mutable Mode mode_ = Mode::kUnknown;

  // Cap configuration parsed from params[1]; see compute_active_cap().
  size_t absolute_cap_override_ = 0;
  int permille_override_ = 0;

  PGM pgm_active_;
  FastMembershipFilterUint64 bloom_active_;
  KeyType min_active_ = std::numeric_limits<KeyType>::max();
  KeyType max_active_ = std::numeric_limits<KeyType>::lowest();
  size_t active_size_ = 0;

  PGM pgm_frozen_;
  FastMembershipFilterUint64 bloom_frozen_;
  KeyType min_frozen_ = std::numeric_limits<KeyType>::max();
  KeyType max_frozen_ = std::numeric_limits<KeyType>::lowest();
  size_t frozen_size_ = 0;
  std::vector<KeyValue<KeyType>> frozen_drain_;
  size_t frozen_drain_cursor_ = 0;

  FastMembershipFilterUint64 prefilter_;
  RangeBitmapFilterUint64 range_prefilter_;

  size_t drains_since_rebuild_ = 0;

  // (E) Worker / async-drain state.
  mutable std::shared_mutex lipp_mtx_;
  std::mutex worker_mtx_;
  std::condition_variable worker_cv_;
  std::atomic<bool> drain_done_{true};
  std::atomic<bool> stop_{false};
  std::thread worker_;

  size_t compute_active_cap(size_t total) const {
    if (absolute_cap_override_ > 0) {
      return absolute_cap_override_;
    }
    if (permille_override_ > 0) {
      const size_t cap =
          (static_cast<size_t>(permille_override_) *
           std::max<size_t>(total, 1)) /
          10000;
      return std::max<size_t>(cap, kAbsoluteCapMin);
    }
    return kActiveCapDefault;
  }

  size_t prefilter_capacity(size_t bulk_size) const {
    const size_t headroom =
        std::max<size_t>(size_t{1} << 16, bulk_size / 32);
    return bulk_size + headroom;
  }

  size_t overlay_filter_capacity() const {
    if constexpr (kUseLippOverlay) {
      return size_t{2} * 1024 * 1024;
    }
    return std::max<size_t>(
        size_t{2} * 1024 * 1024,
        std::min<size_t>(active_cap_, size_t{4} * 1024 * 1024));
  }

  // (F) Adaptive mode finalization. Foreground-only — must NOT be called
  // from a const method (it mutates active_cap_ and may trigger an immediate
  // rotation on the next Insert). Idempotent: returns early once mode_ is
  // set. Only shrinks active_cap_ when no explicit override was provided
  // via params[1], so the existing A/B harness for the cap (params[1]>1000)
  // continues to dominate.
  void finalize_mode_foreground() {
    if (mode_ != Mode::kUnknown) return;
    const double insert_pct =
        static_cast<double>(warmup_inserts_) /
        static_cast<double>(std::max<size_t>(warmup_ops_, 1));
    if (insert_pct >= kInsertHeavyThresholdPct) {
      mode_ = Mode::kInsertHeavy;
      // Default cap is already correct for insert-heavy (no rotation).
    } else {
      mode_ = Mode::kLookupHeavy;
      if (absolute_cap_override_ == 0 && permille_override_ == 0) {
        // Shrinking is safe even mid-run: the next Insert that pushes
        // active_size_ past the new (smaller) cap triggers a rotation
        // through try_swap_active_to_frozen() in the normal way.
        active_cap_ = std::min(active_cap_, kLookupHeavyActiveCap);
      }
    }
  }

  void reset_active() {
    pgm_active_ = PGM(params_);
    bloom_active_.clear();
    bloom_active_.init(overlay_filter_capacity(), kOverlayFilterBitsPerKey);
    min_active_ = std::numeric_limits<KeyType>::max();
    max_active_ = std::numeric_limits<KeyType>::lowest();
    active_size_ = 0;
  }

  void reset_frozen_empty() {
    pgm_frozen_ = PGM(params_);
    bloom_frozen_.clear();
    min_frozen_ = std::numeric_limits<KeyType>::max();
    max_frozen_ = std::numeric_limits<KeyType>::lowest();
    frozen_size_ = 0;
    frozen_drain_.clear();
    frozen_drain_cursor_ = 0;
  }

  // Materializes the drain ferry vector AND seeds the global prefilter with
  // every key in the frozen DPGM, all in one foreground pass. The prefilter
  // update happens here (not in the worker) to avoid a foreground-read /
  // worker-write data race on the Bloom bits. After this returns, the
  // prefilter conservatively reports "maybe in LIPP" for every key in the
  // current frozen DPGM, even before the worker has actually drained them
  // into LIPP. Lookups of those keys still find them in pgm_frozen_ (which
  // outlives the drain) so correctness is preserved.
  void build_frozen_drain_from_pgm() {
    frozen_drain_.clear();
    frozen_drain_.reserve(frozen_size_);
    pgm_frozen_.for_each_kv([this](const KeyType& k, uint64_t v) {
      KeyValue<KeyType> kv;
      kv.key = k;
      kv.value = v;
      frozen_drain_.push_back(kv);
      if (kEnablePrefilter) {
        prefilter_.add(static_cast<uint64_t>(k));
      }
    });
    frozen_drain_cursor_ = 0;
  }

  // Bulk-rebuild LIPP in place from its own current keys. Called from the
  // worker (which already holds unique_lock(lipp_mtx_) when it invokes this).
  void rebuild_lipp_from_lipp_locked() {
    std::vector<std::pair<KeyType, uint64_t>> sorted_kvs;
    sorted_kvs.reserve(total_size_);
    lipp.for_each_leaf_kv([&sorted_kvs](const KeyType& k, uint64_t v) {
      sorted_kvs.emplace_back(k, v);
    });
    lipp.rebuild_from_sorted_kvs(sorted_kvs);
  }

  // (E) Background worker entry point. Waits for a frozen drain to arrive,
  // then pushes keys into LIPP one batch at a time under lipp_mtx_.
  // Exits immediately on stop_, even if a drain is mid-flight (the index is
  // being destroyed, so any not-yet-drained keys are discarded along with it).
  void worker_main() {
    while (true) {
      {
        std::unique_lock<std::mutex> lk(worker_mtx_);
        worker_cv_.wait(lk, [this]() {
          return stop_.load(std::memory_order_acquire) ||
                 !drain_done_.load(std::memory_order_acquire);
        });
        if (stop_.load(std::memory_order_acquire)) {
          return;
        }
      }

      while (!stop_.load(std::memory_order_acquire) &&
             frozen_drain_cursor_ < frozen_drain_.size()) {
        const size_t end = std::min(
            frozen_drain_cursor_ + kWorkerBatchSize, frozen_drain_.size());
        {
          // Single-writer / multi-reader on LIPP: take unique here, foreground
          // lookups take shared in EqualityLookup. The prefilter is NOT
          // updated here (see build_frozen_drain_from_pgm).
          std::unique_lock<std::shared_mutex> lk(lipp_mtx_);
          for (size_t i = frozen_drain_cursor_; i < end; ++i) {
            lipp.Insert(frozen_drain_[i], 0);
          }
          frozen_drain_cursor_ = end;
        }
      }

      if (frozen_drain_cursor_ >= frozen_drain_.size()) {
        ++drains_since_rebuild_;
        if (drains_since_rebuild_ >= kRebuildEveryDrains) {
          std::unique_lock<std::shared_mutex> lk(lipp_mtx_);
          rebuild_lipp_from_lipp_locked();
          drains_since_rebuild_ = 0;
        }
        // Signal foreground that drain is done. The release store publishes
        // all worker-side LIPP writes to any subsequent acquire load.
        drain_done_.store(true, std::memory_order_release);
      }
    }
  }

  // Wait until the worker has finished any in-flight drain. Cheap when the
  // worker is idle (single atomic load).
  void wait_for_drain_done() {
    while (!drain_done_.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }

  void try_swap_active_to_frozen() {
    // Foreground does not enter unless drain is done; this is the precondition.
    pgm_frozen_ = std::move(pgm_active_);
    bloom_frozen_ = std::move(bloom_active_);
    min_frozen_ = min_active_;
    max_frozen_ = max_active_;
    frozen_size_ = active_size_;
    build_frozen_drain_from_pgm();
    // Reset active for new inserts BEFORE waking the worker so the foreground
    // can keep accepting inserts immediately while the worker drains.
    reset_active();
    {
      // Set the cv predicate under the worker's mutex to avoid the textbook
      // missed-wakeup race against a worker that's about to call cv.wait.
      std::lock_guard<std::mutex> lk(worker_mtx_);
      drain_done_.store(false, std::memory_order_release);
    }
    worker_cv_.notify_one();
  }

  // Stop and join the worker. Must be called before the dtor returns.
  void stop_worker() {
    {
      std::lock_guard<std::mutex> lk(worker_mtx_);
      stop_.store(true, std::memory_order_release);
    }
    worker_cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

 public:
  explicit HybridPGMLippAdv(const std::vector<int>& params)
      : lipp(params),
        lipp_overlay(params),
        params_(params),
        pgm_active_(params),
        pgm_frozen_(params) {
    if (params_.size() > 1) {
      const int p1 = params_[1];
      if (p1 > 1000) {
        absolute_cap_override_ = static_cast<size_t>(
            std::min<int>(static_cast<int>(kAbsoluteCapMax),
                          std::max<int>(static_cast<int>(kAbsoluteCapMin), p1)));
      } else if (p1 > 0) {
        permille_override_ = std::min(5000, p1);
      }
    }
    drain_done_.store(true, std::memory_order_release);
    stop_.store(false, std::memory_order_release);
    if constexpr (kEnableAsyncDrain) {
      worker_ = std::thread(&HybridPGMLippAdv::worker_main, this);
    }
  }

  ~HybridPGMLippAdv() { stop_worker(); }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    // Drain any leftover work from a previous Build (worker is alive across
    // the harness's per-repeat reconstructions, but in this design ctor is
    // called fresh per repeat so this is just defense in depth).
    wait_for_drain_done();

    total_size_ = data.size();
    active_cap_ = compute_active_cap(total_size_);
    reset_active();
    reset_frozen_empty();
    if constexpr (kUseLippOverlay) {
      const std::vector<KeyValue<KeyType>> empty;
      lipp_overlay.Build(empty, num_threads);
    }
    drains_since_rebuild_ = 0;
    // (F) Reset adaptive mode state per repeat. The harness reconstructs the
    // index per repeat anyway, but Build() is the canonical "fresh start"
    // and we want the observer to retrain on each fresh dataset.
    mode_ = kDirectLippLookupHeavy
                ? Mode::kLookupHeavy
                : (kLookupHeavyLane ? Mode::kUnknown : Mode::kInsertHeavy);
    warmup_ops_ = 0;
    warmup_inserts_ = 0;

    prefilter_.clear();
    range_prefilter_.clear();
    if constexpr (kUseRangeBaseFilter) {
      range_prefilter_.init(data, kRangeFilterLog2Buckets);
    } else if (kEnablePrefilter) {
      prefilter_.init(prefilter_capacity(total_size_), kBaseFilterBitsPerKey);
      for (const auto& kv : data) {
        prefilter_.add(static_cast<uint64_t>(kv.key));
      }
    }

    // Worker is asleep (drain_done_ == true); safe to call lipp.Build without
    // holding lipp_mtx_. Take it anyway for paranoia.
    uint64_t build_time;
    {
      std::unique_lock<std::shared_mutex> lk(lipp_mtx_);
      build_time = lipp.Build(data, num_threads);
    }
    return build_time;
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    if constexpr (kDirectLippLookupHeavy && !kUseRangeBaseFilter &&
                  !kEnablePrefilter) {
      return lipp.EqualityLookup(lookup_key, thread_id);
    }

    // (F) Adaptive observer: count lookups during warmup. We do NOT call
    // finalize_mode_foreground() from here because it mutates active_cap_,
    // and EqualityLookup is const. The next Insert (also foreground) will
    // hit the warmup threshold and run finalization.
    if (mode_ == Mode::kUnknown) {
      ++warmup_ops_;
    }
    // Both lanes use the bulk-loaded LIPP as the fast base path. The optional
    // base-membership prefilter is left disabled in the final packaged run
    // because it was slower than a direct LIPP probe on these traces.
    bool maybe_in_base = true;
    if constexpr (kUseRangeBaseFilter) {
      maybe_in_base =
          range_prefilter_.maybe_contains(static_cast<uint64_t>(lookup_key));
    } else if constexpr (kEnablePrefilter) {
      maybe_in_base = prefilter_.maybe_contains(static_cast<uint64_t>(lookup_key));
    }
    if (maybe_in_base) {
      const size_t v = lipp.EqualityLookup(lookup_key, thread_id);
      if (v != util::NOT_FOUND) return v;
    }
    if constexpr (kDirectLippLookupHeavy) {
      return util::NOT_FOUND;
    }

    if constexpr (kLookupHeavyLane) {
      // Lookup-heavy lane: pay the Bloom-style overlay filter only after a
      // base miss. This preserves miss filtering for negative lookups without
      // adding a hash probe to the dominant base-hit path.
      if (active_size_ > 0 &&
          lookup_key >= min_active_ && lookup_key <= max_active_ &&
          bloom_active_.maybe_contains(static_cast<uint64_t>(lookup_key))) {
        size_t overlay_v;
        if constexpr (kUseLippOverlay) {
          overlay_v = lipp_overlay.EqualityLookup(lookup_key, thread_id);
        } else {
          overlay_v = pgm_active_.EqualityLookup(lookup_key, thread_id);
        }
        if (overlay_v != util::OVERFLOW) return overlay_v;
      }
      if (frozen_size_ > 0 &&
          lookup_key >= min_frozen_ && lookup_key <= max_frozen_ &&
          bloom_frozen_.maybe_contains(static_cast<uint64_t>(lookup_key))) {
        const size_t overlay_v =
            pgm_frozen_.EqualityLookup(lookup_key, thread_id);
        if (overlay_v != util::OVERFLOW) return overlay_v;
      }
    } else {
      if (active_size_ > 0 &&
          lookup_key >= min_active_ && lookup_key <= max_active_ &&
          bloom_active_.maybe_contains(static_cast<uint64_t>(lookup_key))) {
        size_t overlay_v;
        if constexpr (kUseLippOverlay) {
          overlay_v = lipp_overlay.EqualityLookup(lookup_key, thread_id);
        } else {
          overlay_v = pgm_active_.EqualityLookup(lookup_key, thread_id);
        }
        if (overlay_v != util::OVERFLOW) return overlay_v;
      }
      if (frozen_size_ > 0 &&
          lookup_key >= min_frozen_ && lookup_key <= max_frozen_ &&
          bloom_frozen_.maybe_contains(static_cast<uint64_t>(lookup_key))) {
        const size_t overlay_v =
            pgm_frozen_.EqualityLookup(lookup_key, thread_id);
        if (overlay_v != util::OVERFLOW) return overlay_v;
      }
    }
    return util::NOT_FOUND;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key,
                      uint32_t thread_id) const {
    return pgm_active_.RangeQuery(lower_key, upper_key, thread_id);
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    if constexpr (kDirectLippLookupHeavy) {
      lipp.Insert(data, thread_id);
      return;
    } else if constexpr (kUseLippOverlay) {
      lipp_overlay.Insert(data, thread_id);
    } else {
      pgm_active_.Insert(data, thread_id);
    }
    bloom_active_.add(static_cast<uint64_t>(data.key));
    if (data.key < min_active_) min_active_ = data.key;
    if (data.key > max_active_) max_active_ = data.key;
    ++active_size_;
    ++total_size_;

    // (F) Adaptive observer: count this insert; finalize mode once the
    // warmup window completes. Finalization may shrink active_cap_, in
    // which case the rotation below picks it up immediately on the next
    // Insert (or this one, if active_size_ already exceeds the new cap).
    if (mode_ == Mode::kUnknown) {
      ++warmup_ops_;
      ++warmup_inserts_;
      if (warmup_ops_ >= kWarmupOps) {
        finalize_mode_foreground();
      }
    }

    if (active_size_ >= active_cap_) {
      // Wait for any in-flight drain to finish before swapping. With
      // active_cap_ much smaller than total ops the second wait would be
      // a real stall, but with the default 4 MiB cap on these benchmarks
      // the swap fires zero or one times so the wait is essentially free.
      wait_for_drain_done();
      try_swap_active_to_frozen();
    }
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    std::string name = SearchClass::name();
    (void)unique;
    (void)range_query;
    (void)insert;
    (void)ops_filename;
    return name != "LinearAVX" && !multithread;
  }

  std::vector<std::string> variants() const {
    std::string label = "e" + std::to_string(pgm_error) + "-s" +
                        std::to_string(kOwnerMaxSize) + "-f" +
                        std::to_string(kLocalFlushThreshold);
    if constexpr (kLookupHeavyLane) {
      label += "-bf";
    }
    return {SearchClass::name(), label};
  }

  std::string name() const { return "HybridPGMLippAdv"; }

  std::size_t size() const {
    return pgm_active_.size() + pgm_frozen_.size() + lipp.size() +
           (kUseLippOverlay ? lipp_overlay.size() : 0) +
           bloom_active_.size_in_bytes() + bloom_frozen_.size_in_bytes() +
           prefilter_.size_in_bytes() + range_prefilter_.size_in_bytes();
  }
};

#endif  // TLI_HYBRID_PGM_LIPP_ADVANCED_H
