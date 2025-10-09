#include "lru.h"

#include <algorithm>
#include <cassert>

lru::lru(CACHE* cache) : lru(cache, cache->NUM_SET, cache->NUM_WAY) {
  fmt::print("[REPL] {} uses LRU\n", cache->NAME);
}

lru::lru(CACHE* cache, long sets, long ways) : replacement(cache), NUM_WAY(ways), last_used_cycles(static_cast<std::size_t>(sets * ways), 0) {
  fmt::print("[REPL] {} uses LRU\n", cache->NAME);
}

long lru::find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set,
                      const champsim::cache_block* current_set, champsim::address ip,
                      champsim::address full_addr, access_type type)
{
  // Base index into the per-set LRU timestamp array
  const std::size_t base = static_cast<std::size_t>(set) * static_cast<std::size_t>(NUM_WAY);

  // Scan for the least-recently-used among NON-pinned lines
  long best_way = -1;
  uint64_t best_val = std::numeric_limits<uint64_t>::max();

  for (long w = 0; w < NUM_WAY; ++w) {
    if (current_set[w].pinned) continue;                     // skip pinned
    const uint64_t ts = last_used_cycles[base + static_cast<std::size_t>(w)];
    if (ts < best_val) { best_val = ts; best_way = w; }
  }

  if (best_way == -1) {
    // All lines in this set are pinned -> signal bypass to caller
    // CACHE::handle_fill() will convert NUM_WAY to set_end and (except WRITE) may bypass.
    return NUM_WAY;
  }

  return best_way;
}

void lru::replacement_cache_fill(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip, champsim::address victim_addr,
                                 access_type type)
{
  // Mark the way as being used on the current cycle
  last_used_cycles.at((std::size_t)(set * NUM_WAY + way)) = cycle++;
}

void lru::update_replacement_state(uint32_t triggering_cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                   champsim::address victim_addr, access_type type, uint8_t hit)
{
  // Mark the way as being used on the current cycle
  if (hit && access_type{type} != access_type::WRITE) // Skip this for writeback hits
    last_used_cycles.at((std::size_t)(set * NUM_WAY + way)) = cycle++;
}
