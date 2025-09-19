#include "page_stat.h"
#include <string_view>

namespace page_stats {

static std::map<key, counters> g_stat;

void tlb_access(const char* which, uint32_t core, uint64_t vpn, bool is_hit, bool is_instr)
{
  key k{core, vpn, is_instr};
  auto& c = g_stat[k];

  std::string_view w{which};
  if (w == "ITLB") {
    ++c.itlb_acc;
    if (is_hit) ++c.itlb_hit;
  } else if (w == "DTLB") {
    ++c.dtlb_acc;
    if (is_hit) ++c.dtlb_hit;
  } else if (w == "STLB") {
    ++c.stlb_acc;
    if (is_hit) ++c.stlb_hit;
  }
}

const std::map<key, counters>& snapshot() { return g_stat; }

void clear() { g_stat.clear(); }

} // namespace page_stats