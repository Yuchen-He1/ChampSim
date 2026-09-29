#include "cache_stats.h"

cache_stats operator-(cache_stats lhs, cache_stats rhs)
{
  cache_stats result;
  result.pf_requested = lhs.pf_requested - rhs.pf_requested;
  result.pf_issued = lhs.pf_issued - rhs.pf_issued;
  result.pf_useful = lhs.pf_useful - rhs.pf_useful;
  result.pf_useless = lhs.pf_useless - rhs.pf_useless;
  result.pf_fill = lhs.pf_fill - rhs.pf_fill;

  result.bf_queries = lhs.bf_queries - rhs.bf_queries;
  result.bf_positive_queries = lhs.bf_positive_queries - rhs.bf_positive_queries;
  result.bf_false_positive_queries = lhs.bf_false_positive_queries - rhs.bf_false_positive_queries;
  result.hsp_timed_hits = lhs.hsp_timed_hits - rhs.hsp_timed_hits;
  result.hsp_queue_full = lhs.hsp_queue_full - rhs.hsp_queue_full;
  result.ptw_requests_issued = lhs.ptw_requests_issued - rhs.ptw_requests_issued;
  result.bf_counters_per_filter = lhs.bf_counters_per_filter;
  result.bf_total_counters = lhs.bf_total_counters;

  result.hits = lhs.hits - rhs.hits;
  result.misses = lhs.misses - rhs.misses;

  result.total_miss_latency_cycles = lhs.total_miss_latency_cycles - rhs.total_miss_latency_cycles;
  return result;
}
