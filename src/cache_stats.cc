#include "cache_stats.h"

cache_stats operator-(cache_stats lhs, cache_stats rhs)
{
  cache_stats result;
  result.pf_requested = lhs.pf_requested - rhs.pf_requested;
  result.pf_issued = lhs.pf_issued - rhs.pf_issued;
  result.pf_useful = lhs.pf_useful - rhs.pf_useful;
  result.pf_useless = lhs.pf_useless - rhs.pf_useless;
  result.pf_fill = lhs.pf_fill - rhs.pf_fill;

  result.hits = lhs.hits - rhs.hits;
  result.misses = lhs.misses - rhs.misses;
  
  result.total_miss_latency_cycles = lhs.total_miss_latency_cycles - rhs.total_miss_latency_cycles;

   // ADD
  result.pin_lines         = lhs.pin_lines         - rhs.pin_lines;
  result.pin_hits          = lhs.pin_hits          - rhs.pin_hits;
  result.pin_bypass_on_full= lhs.pin_bypass_on_full- rhs.pin_bypass_on_full;
  result.pin_evicted_colder= lhs.pin_evicted_colder- rhs.pin_evicted_colder;
  return result;
}
