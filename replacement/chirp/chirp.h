#ifndef REPLACEMENT_CHIRP_H
#define REPLACEMENT_CHIRP_H

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "cache.h"
#include "modules.h"

// Mirbagher-Ajorpaz et al., CHiRP, MICRO 2020, Sections IV and V.
struct chirp : champsim::modules::replacement {
  static constexpr std::size_t TABLE_SIZE = 4096; // 1 KiB of logical 2-bit counters
  static constexpr uint8_t DEAD_THRESHOLD = 1;
  static constexpr uint8_t COUNTER_MAX = 3;

  struct entry {
    uint16_t signature = 0;
    bool valid = false;
    bool dead = false;
    bool first_hit = true;
    uint64_t last_used = 0;
  };

  long num_way;
  std::vector<entry> entries;
  std::vector<uint8_t> prediction_table;
  std::map<uint32_t, uint64_t> path_history;
  // Retain the first request's signature across latency and MSHR merging.
  std::map<std::pair<uint32_t, uint64_t>, uint16_t> pending_signatures;
  uint64_t access_clock = 0;

  explicit chirp(CACHE* cache);
  chirp(CACHE* cache, long sets, long ways);

  static uint16_t hash_signature(uint64_t value);
  uint16_t access_signature(uint32_t cpu, champsim::address ip);
  std::pair<uint32_t, uint64_t> miss_key(uint32_t cpu, champsim::address address) const;
  entry& get_entry(long set, long way);
  uint8_t& counter(uint16_t signature);

  long find_victim(uint32_t cpu, uint64_t instr_id, long set, const champsim::cache_block* current_set, champsim::address ip,
                   champsim::address full_addr, access_type type);
  void update_replacement_state(uint32_t cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                champsim::address victim_addr, access_type type, uint8_t hit);
  void replacement_cache_fill(uint32_t cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                              champsim::address victim_addr, access_type type);
};

#endif
