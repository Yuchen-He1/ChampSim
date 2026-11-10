#include "cache_stats.h"

cache_stats operator-(cache_stats lhs, cache_stats rhs)
{
  cache_stats result;
  result.victima_occupancy = lhs.victima_occupancy;
  result.victima_lookup_hit = lhs.victima_lookup_hit - rhs.victima_lookup_hit;
  result.victima_lookup_miss = lhs.victima_lookup_miss - rhs.victima_lookup_miss;
  result.victima_demand_insert = lhs.victima_demand_insert - rhs.victima_demand_insert;
  result.victima_background_insert = lhs.victima_background_insert - rhs.victima_background_insert;
  result.victima_background_walk = lhs.victima_background_walk - rhs.victima_background_walk;
  result.victima_merge = lhs.victima_merge - rhs.victima_merge;
  result.victima_queue_drop = lhs.victima_queue_drop - rhs.victima_queue_drop;
  result.victima_retry = lhs.victima_retry - rhs.victima_retry;
  result.victima_stale_result = lhs.victima_stale_result - rhs.victima_stale_result;
  result.victima_completion = lhs.victima_completion - rhs.victima_completion;
  result.victima_data_eviction = lhs.victima_data_eviction - rhs.victima_data_eviction;
  result.victima_translation_eviction = lhs.victima_translation_eviction - rhs.victima_translation_eviction;
  result.victima_data_miss = lhs.victima_data_miss - rhs.victima_data_miss;

  result.pf_requested = lhs.pf_requested - rhs.pf_requested;
  result.pf_issued = lhs.pf_issued - rhs.pf_issued;
  result.pf_useful = lhs.pf_useful - rhs.pf_useful;
  result.pf_useless = lhs.pf_useless - rhs.pf_useless;
  result.pf_fill = lhs.pf_fill - rhs.pf_fill;

  result.hits = lhs.hits - rhs.hits;
  result.misses = lhs.misses - rhs.misses;

  result.total_miss_latency_cycles = lhs.total_miss_latency_cycles - rhs.total_miss_latency_cycles;
  return result;
}
