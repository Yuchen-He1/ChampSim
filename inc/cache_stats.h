#ifndef CACHE_STATS_H
#define CACHE_STATS_H

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "channel.h"
#include "event_counter.h"

struct cache_stats {
  std::string name;
  uint64_t victima_lookup_hit = 0;
  uint64_t victima_lookup_miss = 0;
  uint64_t victima_demand_insert = 0;
  uint64_t victima_background_insert = 0;
  uint64_t victima_background_walk = 0;
  uint64_t victima_merge = 0;
  uint64_t victima_queue_drop = 0;
  uint64_t victima_retry = 0;
  uint64_t victima_stale_result = 0;
  uint64_t victima_completion = 0;
  uint64_t victima_data_eviction = 0;
  uint64_t victima_translation_eviction = 0;
  uint64_t victima_data_miss = 0;
  uint64_t victima_occupancy = 0;

  // prefetch stats
  uint64_t pf_requested = 0;
  uint64_t pf_issued = 0;
  uint64_t pf_useful = 0;
  uint64_t pf_useless = 0;
  uint64_t pf_fill = 0;

  champsim::stats::event_counter<std::pair<access_type, std::remove_cv_t<decltype(NUM_CPUS)>>> hits = {};
  champsim::stats::event_counter<std::pair<access_type, std::remove_cv_t<decltype(NUM_CPUS)>>> misses = {};
  champsim::stats::event_counter<std::pair<access_type, std::remove_cv_t<decltype(NUM_CPUS)>>> mshr_merge = {};
  champsim::stats::event_counter<std::pair<access_type, std::remove_cv_t<decltype(NUM_CPUS)>>> mshr_return = {};

  long total_miss_latency_cycles{};
};

cache_stats operator-(cache_stats lhs, cache_stats rhs);

#endif
