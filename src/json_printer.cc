/*
 *    Copyright 2023 The ChampSim Contributors
 *    (License header unchanged)
 */

#include <algorithm>
#include <utility>
#include <nlohmann/json.hpp>

#include "stats_printer.h"
#include "page_stat.h"

void to_json(nlohmann::json& j, const O3_CPU::stats_type& stats)
{
  constexpr std::array types{branch_type::BRANCH_DIRECT_JUMP, branch_type::BRANCH_INDIRECT,      branch_type::BRANCH_CONDITIONAL,
                             branch_type::BRANCH_DIRECT_CALL, branch_type::BRANCH_INDIRECT_CALL, branch_type::BRANCH_RETURN};

  auto total_mispredictions = std::ceil(
      std::accumulate(std::begin(types), std::end(types), 0LL, [btm = stats.branch_type_misses](auto acc, auto next) { return acc + btm.value_or(next, 0); }));

  std::map<std::string, std::size_t> mpki{};
  for (auto type : types) {
    mpki.emplace(branch_type_names.at(champsim::to_underlying(type)), stats.branch_type_misses.value_or(type, 0));
  }

  j = nlohmann::json{{"instructions", stats.instrs()},
                     {"cycles", stats.cycles()},
                     {"Avg ROB occupancy at mispredict", std::ceil(stats.total_rob_occupancy_at_branch_mispredict) / std::ceil(total_mispredictions)},
                     {"mispredict", mpki}};
}

void to_json(nlohmann::json& j, const CACHE::stats_type& stats)
{
  using hits_value_type = typename decltype(stats.hits)::value_type;
  using misses_value_type = typename decltype(stats.misses)::value_type;
  using mshr_merge_value_type = typename decltype(stats.mshr_merge)::value_type;
  using mshr_return_value_type = typename decltype(stats.mshr_return)::value_type;

  std::map<std::string, nlohmann::json> statsmap;
  statsmap.emplace("prefetch requested", stats.pf_requested);
  statsmap.emplace("prefetch issued", stats.pf_issued);
  statsmap.emplace("useful prefetch", stats.pf_useful);
  statsmap.emplace("useless prefetch", stats.pf_useless);

  uint64_t total_downstream_demands = stats.mshr_return.total();
  for (std::size_t cpu = 0; cpu < NUM_CPUS; ++cpu)
    total_downstream_demands -= stats.mshr_return.value_or(std::pair{access_type::PREFETCH, cpu}, mshr_return_value_type{});

  statsmap.emplace("miss latency", std::ceil(stats.total_miss_latency_cycles) / std::ceil(total_downstream_demands));
  for (const auto type : {access_type::LOAD, access_type::RFO, access_type::PREFETCH, access_type::WRITE, access_type::TRANSLATION}) {
    std::vector<hits_value_type> hits;
    std::vector<misses_value_type> misses;
    std::vector<mshr_merge_value_type> mshr_merges;

    for (std::size_t cpu = 0; cpu < NUM_CPUS; ++cpu) {
      hits.push_back(stats.hits.value_or(std::pair{type, cpu}, hits_value_type{}));
      misses.push_back(stats.misses.value_or(std::pair{type, cpu}, misses_value_type{}));
      mshr_merges.push_back(stats.mshr_merge.value_or(std::pair{type, cpu}, mshr_merge_value_type{}));
    }

    statsmap.emplace(access_type_names.at(champsim::to_underlying(type)), nlohmann::json{{"hit", hits}, {"miss", misses}, {"mshr_merge", mshr_merges}});
  }

  j = statsmap;
}

void to_json(nlohmann::json& j, const DRAM_CHANNEL::stats_type stats)
{
  j = nlohmann::json{{"RQ ROW_BUFFER_HIT", stats.RQ_ROW_BUFFER_HIT},
                     {"RQ ROW_BUFFER_MISS", stats.RQ_ROW_BUFFER_MISS},
                     {"WQ ROW_BUFFER_HIT", stats.WQ_ROW_BUFFER_HIT},
                     {"WQ ROW_BUFFER_MISS", stats.WQ_ROW_BUFFER_MISS},
                     {"AVG DBUS CONGESTED CYCLE", (std::ceil(stats.dbus_cycle_congested) / std::ceil(stats.dbus_count_congested))},
                     {"REFRESHES ISSUED", stats.refresh_cycles}};
}

namespace champsim
{
void to_json(nlohmann::json& j, const champsim::phase_stats stats)
{
  std::map<std::string, nlohmann::json> roi_stats;
  roi_stats.emplace("cores", stats.roi_cpu_stats);
  roi_stats.emplace("DRAM", stats.roi_dram_stats);
  for (auto x : stats.roi_cache_stats) {
    roi_stats.emplace(x.name, x);
  }

  std::map<std::string, nlohmann::json> sim_stats;
  sim_stats.emplace("cores", stats.sim_cpu_stats);
  sim_stats.emplace("DRAM", stats.sim_dram_stats);
  for (auto x : stats.sim_cache_stats) {
    sim_stats.emplace(x.name, x);
  }

  std::map<std::string, nlohmann::json> statsmap{{"name", stats.name}, {"traces", stats.trace_names}};
  statsmap.emplace("roi", roi_stats);
  statsmap.emplace("sim", sim_stats);
  j = statsmap;
}
} // namespace champsim

void champsim::json_printer::print(std::vector<phase_stats>& stats)
{
  // Original per-phase output
  nlohmann::json phases = nlohmann::json::array_t{std::begin(stats), std::end(stats)};

  // Build per-page translation list (sorted)
  nlohmann::json j_pages = nlohmann::json::array();

  auto div = [](uint64_t num, uint64_t den) -> double {
    return den ? static_cast<double>(num) / static_cast<double>(den) : 0.0;
  };

  // Pull snapshot into a vector so we can sort
  struct Row {
    uint32_t core;
    uint64_t vpn;
    bool is_instr;
    uint64_t itlb_acc, itlb_hit;
    uint64_t dtlb_acc, dtlb_hit;
    uint64_t stlb_acc, stlb_hit;
  };

  std::vector<Row> rows;
  for (const auto& kv : page_stats::snapshot()) {
    const auto& k = kv.first;
    const auto& c = kv.second;
    rows.push_back(Row{k.core, k.vpn, k.is_instr, c.itlb_acc, c.itlb_hit, c.dtlb_acc, c.dtlb_hit, c.stlb_acc, c.stlb_hit});
  }

  // Sort by "hotness":
  //   1) total TLB acc (itlb_acc + dtlb_acc) desc
  //   2) stlb_acc desc
  //   3) core asc
  //   4) vpn  asc
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b){
    const auto a_tlb = a.itlb_acc + a.dtlb_acc;
    const auto b_tlb = b.itlb_acc + b.dtlb_acc;
    if (a_tlb != b_tlb) return a_tlb > b_tlb;
    if (a.stlb_acc != b.stlb_acc) return a.stlb_acc > b.stlb_acc;
    if (a.core != b.core) return a.core < b.core;
    return a.vpn < b.vpn;
  });

  for (const auto& r : rows) {
    const uint64_t tlb_acc_total = r.itlb_acc + r.dtlb_acc;
    const uint64_t stlb_ptw = (r.stlb_acc >= r.stlb_hit) ? (r.stlb_acc - r.stlb_hit) : 0;

    nlohmann::json row = {
      {"core", r.core},
      {"vpn",  r.vpn},
      {"is_instr", r.is_instr},
      {"itlb_hit_rate", div(r.itlb_hit, r.itlb_acc)},
      {"dtlb_hit_rate", div(r.dtlb_hit, r.dtlb_acc)},
      {"stlb_hit_rate", div(r.stlb_hit, r.stlb_acc)},
      // PTW rate = PTW count / total TLB accesses
      {"ptw_rate", div(stlb_ptw, tlb_acc_total)},
      {"raw", {
        {"itlb_acc", r.itlb_acc}, {"itlb_hit", r.itlb_hit},
        {"dtlb_acc", r.dtlb_acc}, {"dtlb_hit", r.dtlb_hit},
        {"stlb_acc", r.stlb_acc}, {"stlb_hit", r.stlb_hit},
        {"stlb_ptw", stlb_ptw}
      }}
    };
    j_pages.push_back(std::move(row));
  }

  nlohmann::json root;
  root["phases"] = std::move(phases);
  root["per_page_translation"] = std::move(j_pages);
  stream << root;
}