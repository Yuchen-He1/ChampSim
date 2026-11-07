#include <catch.hpp>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "cache.h"
#include "defaults.hpp"
#include "stats_printer.h"

void to_json(nlohmann::json& j, const CACHE::stats_type& stats);

namespace
{
struct bf_environment {
  std::vector<std::pair<std::string, std::optional<std::string>>> saved;
  explicit bf_environment(const char* hashes = "1", const char* buffers = "1")
  {
    for (const auto& [key, value] : std::vector<std::pair<std::string, std::string>>{
             {"CHAMPSIM_STLB_HSP_BUFFER_CAPACITY", "512"}, {"CHAMPSIM_STLB_HSP_NUM_BUFFERS", buffers},
             {"CHAMPSIM_STLB_HSP_BF_COUNTERS", "1"}, {"CHAMPSIM_STLB_HSP_BF_HASHES", hashes},
             {"CHAMPSIM_STLB_HSP_INTERNAL_LATENCY", "0"}, {"CHAMPSIM_STLB_HSP_PROMOTE_THRESHOLD", "1"}, {"CHAMPSIM_STLB_HSP_HOTNESS_SAT", "512"}}) {
      const auto* previous = std::getenv(key.c_str());
      saved.emplace_back(key, previous ? std::optional<std::string>{previous} : std::nullopt);
      setenv(key.c_str(), value.c_str(), 1);
    }
  }
  ~bf_environment()
  {
    for (const auto& [key, value] : saved) {
      if (value) {
        setenv(key.c_str(), value->c_str(), 1);
      } else {
        unsetenv(key.c_str());
      }
    }
  }
};

struct bf_rig {
  champsim::channel upper{}, lower{};
  CACHE cache{champsim::cache_builder{champsim::defaults::default_stlb}
                  .name("test_STLB")
                  .sets(1)
                  .ways(1)
                  .hit_latency(1)
                  .fill_latency(1)
                  .upper_levels({&upper})
                  .lower_level(&lower)};

  bf_rig()
  {
    cache.initialize();
    cache.warmup = false;
    cache.begin_phase();
  }
  void tick(int count = 4)
  {
    for (int i = 0; i < count; ++i) {
      cache._operate();
    }
  }
  void issue(uint64_t page)
  {
    champsim::channel::request_type request{};
    request.address = champsim::address{page * 4096};
    request.v_address = request.address;
    request.is_translated = true;
    request.response_requested = true;
    request.cpu = 0;
    request.type = access_type::LOAD;
    REQUIRE(upper.add_rq(request));
    tick();
  }
  void fill(uint64_t page)
  {
    issue(page);
    REQUIRE(lower.RQ.size() == 1);
    auto request = lower.RQ.front();
    lower.RQ.pop_front();
    request.data = champsim::address{page * 4096 + 0x1000000};
    lower.returned.emplace_back(request);
    tick();
    REQUIRE(upper.returned.size() == 1);
    upper.returned.clear();
  }
};
} // namespace

TEST_CASE("BF saturation counts clipped updates, not reaching the maximum", "[bf-statistics]")
{
  bf_environment env;
  bf_rig rig;
  const uint64_t maximum = (uint64_t{1} << CACHE::bf_counter_bits()) - 1;
  // The first page stays in STLB; subsequent fills insert HSP members.
  for (uint64_t page = 1; page <= maximum + 1; ++page) {
    rig.fill(page);
  }
  CHECK(rig.cache.sim_stats.bf_counter_saturations == 0);
  rig.fill(maximum + 2);
  CHECK(rig.cache.sim_stats.bf_counter_saturations == 1);
  CHECK(rig.cache.sim_stats.bf_false_negatives == 0);
  rig.cache.end_phase(0);
  CHECK(rig.cache.roi_stats.bf_counter_saturations == 1);
  rig.cache.begin_phase();
  CHECK(rig.cache.sim_stats.bf_counter_saturations == 0);
  rig.fill(maximum + 3);
  CHECK(rig.cache.sim_stats.bf_counter_saturations == 1);
}

TEST_CASE("BF saturation counts each clipped hash update", "[bf-statistics]")
{
  bf_environment env{"7"};
  bf_rig rig;
  const uint64_t maximum = (uint64_t{1} << CACHE::bf_counter_bits()) - 1;
  const uint64_t insertions = maximum / 7 + 1;
  for (uint64_t page = 1; page <= insertions + 1; ++page) {
    rig.fill(page);
  }
  CHECK(rig.cache.sim_stats.bf_counter_saturations == insertions * 7 - maximum);
}

TEST_CASE("BF detects a real false negative after saturation and deletion", "[bf-statistics]")
{
  const auto buffers = GENERATE(1, 2);
  bf_environment env{"1", buffers == 1 ? "1" : "2"};
  bf_rig rig;
  const uint64_t maximum = (uint64_t{1} << CACHE::bf_counter_bits()) - 1;
  const auto last_page = (maximum + 1) * static_cast<uint64_t>(buffers) + 1;
  for (uint64_t page = 1; page <= last_page; ++page) {
    rig.fill(page);
  }
  REQUIRE(rig.cache.sim_stats.bf_counter_saturations == static_cast<uint64_t>(buffers));
  rig.cache.begin_phase(); // The statistics reset; live HSP/BF membership survives.
  uint64_t stlb_page = last_page;
  for (uint64_t i = 0; i < maximum; ++i) {
    const uint64_t page = 1 + i * static_cast<uint64_t>(buffers);
    // Leave a free STLB slot so promotion removes the HSP member without a swap.
    rig.cache.invalidate_entry(champsim::address{stlb_page * 4096});
    rig.issue(page);
    REQUIRE(rig.upper.returned.size() == 1);
    REQUIRE(rig.lower.RQ.empty());
    rig.upper.returned.clear();
    stlb_page = page;
  }
  // One page is still resident in buffer 0, whose clipped count is now zero.
  // With two buffers, buffer 1 is positive but cannot satisfy this lookup.
  rig.issue(1 + maximum * static_cast<uint64_t>(buffers));
  CHECK(rig.upper.returned.empty());
  CHECK(rig.lower.RQ.size() == 1); // Observation must not repair/bypass the BF.
  CHECK(rig.cache.sim_stats.bf_false_negatives == 1);
  CHECK(rig.cache.sim_stats.bf_false_positives == 0);
  CHECK(rig.cache.sim_stats.bf_queries == maximum + 1);
  rig.cache.end_phase(0);
  CHECK(rig.cache.roi_stats.bf_false_negatives == 1);
}

TEST_CASE("BF statistics distinguish true misses and false positives from false negatives", "[bf-statistics]")
{
  bf_environment env;
  bf_rig rig;
  rig.fill(1); // Empty filter: a true miss, not a false negative.
  CHECK(rig.cache.sim_stats.bf_false_negatives == 0);
  rig.fill(2);
  rig.issue(1); // A real HSP hit, with a promotion/swap.
  REQUIRE(rig.upper.returned.size() == 1);
  rig.upper.returned.clear();
  rig.issue(3); // Single occupied counter: positive, but VPN 3 is not resident.
  CHECK(rig.cache.sim_stats.bf_false_positives == 1);
  CHECK(rig.cache.sim_stats.bf_false_negatives == 0);
}

TEST_CASE("BF diagnostics survive statistics subtraction and JSON output", "[bf-statistics]")
{
  cache_stats earlier{}, later{};
  earlier.bf_counter_saturations = 3;
  earlier.bf_false_negatives = 1;
  later.bf_counter_saturations = 11;
  later.bf_false_negatives = 5;
  const auto delta = later - earlier;
  CHECK(delta.bf_counter_saturations == 8);
  CHECK(delta.bf_false_negatives == 4);
  nlohmann::json result;
  to_json(result, delta);
  CHECK(result.at("bf counter saturations") == 8);
  CHECK(result.at("bf false negatives") == 4);

  auto printable = later;
  printable.name = "test_STLB";
  printable.bf_counters_per_filter = 1;
  printable.hits.set({access_type::LOAD, 0}, 1);
  const auto lines = champsim::plain_printer::format(printable);
  CHECK(std::any_of(lines.begin(), lines.end(), [](const auto& line) { return line.find("BF COUNTER SATURATIONS:") != std::string::npos; }));
  CHECK(std::any_of(lines.begin(), lines.end(), [](const auto& line) { return line.find("BF FALSE NEGATIVES:") != std::string::npos; }));

  champsim::phase_stats phase{};
  phase.roi_cache_stats = {printable};
  // Two phases exercise the top-level ROI aggregation, rather than sim stats.
  std::vector<champsim::phase_stats> phases{phase, phase};
  std::ostringstream output;
  champsim::json_printer{output}.print(phases);
  const auto root = nlohmann::json::parse(output.str());
  CHECK(root.at("bf_counter_saturations") == 22);
  CHECK(root.at("bf_false_negatives") == 10);
}
