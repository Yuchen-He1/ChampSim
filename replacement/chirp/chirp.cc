#include "chirp.h"

#include <algorithm>

#include "control_flow_history.h"

chirp::chirp(CACHE* cache) : chirp(cache, cache->NUM_SET, cache->NUM_WAY) {}

chirp::chirp(CACHE* cache, long sets, long ways)
    : replacement(cache), num_way(ways), entries(static_cast<std::size_t>(sets * ways)), prediction_table(TABLE_SIZE, 0)
{
}

uint16_t chirp::hash_signature(uint64_t value)
{
  // 64-bit integer mix from the paper's reference [72]:
  // https://gist.github.com/badboy/6267743 (hash64shift).
  value = (~value) + (value << 21);
  value ^= value >> 24;
  value = (value + (value << 3)) + (value << 8);
  value ^= value >> 14;
  value = (value + (value << 2)) + (value << 4);
  value ^= value >> 28;
  value += value << 31;
  return static_cast<uint16_t>(value);
}

uint16_t chirp::access_signature(uint32_t cpu, champsim::address ip)
{
  auto pc = ip.to<uint64_t>();
  auto& path = path_history[cpu];
  const auto& branches = champsim::cpu_control_flow_history[cpu];
  auto signature = hash_signature((pc >> 2) ^ path ^ branches.conditional ^ branches.indirect);
  // Figure 5, lines 28-30: PC[3:2], with two zero bits per 4-bit slot.
  path = (path << 4) | ((pc >> 2) & 0x3);
  return signature;
}

std::pair<uint32_t, uint64_t> chirp::miss_key(uint32_t cpu, champsim::address address) const
{
  return {cpu, intern_ == nullptr ? address.to<uint64_t>() : address.slice_upper(intern_->OFFSET_BITS).to<uint64_t>()};
}

chirp::entry& chirp::get_entry(long set, long way) { return entries.at(static_cast<std::size_t>(set * num_way + way)); }

uint8_t& chirp::counter(uint16_t signature) { return prediction_table.at(signature % TABLE_SIZE); }

long chirp::find_victim(uint32_t, uint64_t, long set, const champsim::cache_block* current_set, champsim::address, champsim::address, access_type)
{
  // CACHE normally handles invalid ways before calling the replacement module.
  for (long way = 0; way < num_way; ++way)
    if (!current_set[way].valid)
      return way;

  for (long way = 0; way < num_way; ++way)
    if (get_entry(set, way).dead)
      return way;

  long victim = 0;
  for (long way = 1; way < num_way; ++way)
    if (get_entry(set, way).last_used < get_entry(set, victim).last_used)
      victim = way;
  return victim;
}

void chirp::update_replacement_state(uint32_t cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                     champsim::address, access_type type, uint8_t hit)
{
  if (type == access_type::WRITE)
    return;

  // A blocked miss is retried by CACHE on later cycles. Merged accesses to
  // the same translation retain the first request's history as well.
  if (!hit && pending_signatures.count(miss_key(cpu, full_addr)) != 0)
    return;

  auto signature = access_signature(cpu, ip);
  if (!hit) {
    pending_signatures.try_emplace(miss_key(cpu, full_addr), signature);
    return;
  }

  auto& line = get_entry(set, way);
  if (line.first_hit) {
    // Read the new prediction before training, as in Figure 5, lines 6-7.
    const bool dead = counter(signature) > DEAD_THRESHOLD;
    auto& old_counter = counter(line.signature);
    if (line.valid && old_counter > 0)
      --old_counter;
    line.dead = dead;
    line.first_hit = false;
  }
  // Even subsequent hits replace the signature, but do not retrain/re-predict.
  line.signature = signature;
  line.valid = true;
  line.last_used = ++access_clock;
}

void chirp::replacement_cache_fill(uint32_t cpu, long set, long way, champsim::address full_addr, champsim::address ip,
                                   champsim::address, access_type type)
{
  auto& line = get_entry(set, way);
  auto pending = pending_signatures.find(miss_key(cpu, full_addr));
  uint16_t signature = 0;
  if (pending != pending_signatures.end()) {
    signature = pending->second;
    pending_signatures.erase(pending);
    // A shared TLB may merge requests from several CPUs for this translation.
    auto address_key = miss_key(cpu, full_addr).second;
    for (auto it = pending_signatures.begin(); it != pending_signatures.end();) {
      if (it->first.second == address_key)
        it = pending_signatures.erase(it);
      else
        ++it;
    }
  } else if (type != access_type::WRITE) {
    // Direct fills (e.g. module tests) need not have a preceding tag lookup.
    signature = access_signature(cpu, ip);
  }

  const bool dead = type != access_type::WRITE && counter(signature) > DEAD_THRESHOLD;
  // CACHE invokes this callback before overwriting its block. An explicitly
  // invalidated translation is not a live LRU eviction, even with stale metadata.
  const bool victim_valid = line.valid && (intern_ == nullptr || intern_->block.at(static_cast<std::size_t>(set * num_way + way)).valid);
  if (victim_valid && !line.dead) {
    auto& old_counter = counter(line.signature);
    if (old_counter < COUNTER_MAX)
      ++old_counter;
  }
  line = entry{signature, type != access_type::WRITE, dead, true, ++access_clock};
}
