#ifndef CHAMPSIM_PAGE_STAT_H
#define CHAMPSIM_PAGE_STAT_H

#include <cstdint>
#include <map>
#include <vector>

namespace page_stats {

// Key = (core, vpn, is_instr)
struct key {
  uint32_t core{};
  uint64_t vpn{};
  bool is_instr{}; // true = ITLB page, false = DTLB/STLB page

  // map key ordering
  bool operator<(const key& rhs) const {
    if (core != rhs.core) return core < rhs.core;
    if (vpn  != rhs.vpn)  return vpn  < rhs.vpn;
    return is_instr < rhs.is_instr;
  }
};

struct counters {
  uint64_t itlb_acc{0}, itlb_hit{0};
  uint64_t dtlb_acc{0}, dtlb_hit{0};
  uint64_t stlb_acc{0}, stlb_hit{0};
};

// Record an access for TLB or STLB.
// which: "ITLB" | "DTLB" | "STLB"
// core: CPU id
// vpn:  virtual page number
// is_hit: whether it hit at that level
// is_instr: true for I-side (ITLB/STLB for instr), false for D-side
void tlb_access(const char* which, uint32_t core, uint64_t vpn, bool is_hit, bool is_instr);

// Get a snapshot for printing
const std::map<key, counters>& snapshot();

// Reset all stats (optional utility)
void clear();

} // namespace page_stats

#endif // CHAMPSIM_PAGE_STAT_H