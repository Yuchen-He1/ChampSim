#ifndef CHAMPSIM_TRANSLATION_H
#define CHAMPSIM_TRANSLATION_H
#include <array>
#include <cstdint>
#include <map>
#include "address.h"
namespace champsim {
inline std::map<uint32_t, uint64_t> victima_retired, victima_stlb_misses, victima_data_misses;
inline double victima_mpki(const std::map<uint32_t,uint64_t>& m, uint32_t cpu) { auto n=victima_retired[cpu]; auto it=m.find(cpu); return n && it!=m.end() ? 1000.0*static_cast<double>(it->second)/static_cast<double>(n) : 0; }
enum class translation_op { none, lookup, probe, insert_demand, insert_background, walk };
struct translation_block {
  uint64_t cluster = 0;
  uint32_t cpu = 0;
  std::array<uint8_t, 2> asid{255,255};
  uint8_t page_bits = 12;
  std::array<address, 8> ppn{}; // page-aligned physical addresses, never inferred from neighbours
  std::array<bool, 8> valid{};
  address pte_address{};
};
struct translation_message {
  translation_op op = translation_op::none;
  uint64_t id = 0;
  bool hit = false;
  translation_block entries{};
};
}
#endif
