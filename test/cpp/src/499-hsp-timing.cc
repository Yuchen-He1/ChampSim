#include <catch.hpp>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "cache.h"
#include "defaults.hpp"
#include "page_stat.h"

namespace
{
struct hsp_environment {
  std::vector<std::pair<std::string, std::optional<std::string>>> previous;
  void set(const char* key, const char* value) {
    const char* old = std::getenv(key);
    previous.emplace_back(key, old ? std::optional<std::string>{old} : std::nullopt);
    setenv(key, value, 1);
  }
  hsp_environment() {
    set("CHAMPSIM_STLB_HSP_INTERNAL_LATENCY", "1");
    set("CHAMPSIM_STLB_HSP_BUFFER_CAPACITY", "2");
    set("CHAMPSIM_STLB_HSP_NUM_BUFFERS", "1");
    set("CHAMPSIM_STLB_HSP_BF_LATENCY", "1");
    set("CHAMPSIM_STLB_HSP_SEARCH_LATENCY", "9");
    set("CHAMPSIM_STLB_HSP_BF_COUNTERS", "1");
    set("CHAMPSIM_STLB_HSP_BF_HASHES", "1");
    set("CHAMPSIM_STLB_HSP_PROMOTE_THRESHOLD", "1");
  }
  ~hsp_environment() {
    for (auto it = previous.rbegin(); it != previous.rend(); ++it) {
      if (it->second) setenv(it->first.c_str(), it->second->c_str(), 1);
      else unsetenv(it->first.c_str());
    }
  }
};

struct hsp_rig {
  champsim::channel upper{}, other{}, lower{};
  CACHE cache{champsim::cache_builder{champsim::defaults::default_stlb}
                  .name("test_STLB").sets(1).ways(1).mshr_size(2)
                  .hit_latency(2).fill_latency(2)
                  .tag_bandwidth(champsim::bandwidth::maximum_type{1})
                  .fill_bandwidth(champsim::bandwidth::maximum_type{1})
                  .upper_levels({&upper, &other}).lower_level(&lower)};
  uint64_t id = 0;
  hsp_rig() { cache.initialize(); cache.warmup = false; cache.begin_phase(); }
  void tick(int n = 1) { for (int i = 0; i < n; ++i) cache._operate(); }
  void issue(uint64_t address, champsim::channel& source) {
    champsim::channel::request_type pkt{};
    pkt.address = champsim::address{address};
    pkt.v_address = pkt.address;
    pkt.is_translated = true;
    pkt.response_requested = true;
    pkt.type = access_type::LOAD;
    pkt.cpu = 0;
    pkt.instr_id = ++id;
    REQUIRE(source.add_rq(pkt));
  }
  void fill(uint64_t address) {
    issue(address, upper);
    tick(16);
    REQUIRE(lower.RQ.size() == 1);
    auto pkt = lower.RQ.front();
    lower.RQ.pop_front();
    pkt.data = champsim::address{address + 0x100000};
    lower.returned.emplace_back(pkt);
    tick(4);
    REQUIRE(upper.returned.size() == 1);
    upper.returned.clear();
  }
  void seed_hsp() { fill(0x1000); fill(0x2000); }
};
}

TEST_CASE("HSP BF negative waits one cycle before forwarding", "[hsp-timing]")
{
  hsp_environment env;
  hsp_rig rig;
  rig.issue(0x1000, rig.upper);
  rig.tick(3);
  CHECK(rig.lower.RQ.empty());
  rig.tick();
  REQUIRE(rig.lower.RQ.size() == 1);
  CHECK(rig.cache.sim_stats.bf_queries == 1);
  CHECK(rig.cache.sim_stats.bf_positive_queries == 0);
  CHECK(rig.cache.sim_stats.ptw_requests_issued == 1);
}

TEST_CASE("HSP hit waits for BF and search and promotes once", "[hsp-timing]")
{
  hsp_environment env;
  hsp_rig rig;
  rig.seed_hsp();
  rig.issue(0x1000, rig.upper);
  rig.tick(12);
  CHECK(rig.upper.returned.empty());
  CHECK(rig.lower.RQ.empty());
  rig.tick();
  REQUIRE(rig.upper.returned.size() == 1);
  CHECK(rig.upper.returned.front().data == champsim::address{0x101000});
  CHECK(rig.cache.sim_stats.hsp_timed_hits == 1);
  rig.upper.returned.clear();
  rig.issue(0x1000, rig.upper);
  rig.tick(3);
  CHECK(rig.upper.returned.size() == 1);
  CHECK(rig.cache.sim_stats.hsp_timed_hits == 1);
}

TEST_CASE("HSP false positive delays PTW by BF plus search", "[hsp-timing]")
{
  hsp_environment env;
  hsp_rig rig;
  rig.seed_hsp();
  // A one-counter BF is deterministically positive for any key while occupied.
  rig.issue(0x3000, rig.upper);
  rig.tick(12);
  CHECK(rig.lower.RQ.empty());
  rig.tick();
  REQUIRE(rig.lower.RQ.size() == 1);
  CHECK(rig.cache.sim_stats.bf_false_positive_queries == 1);
}

TEST_CASE("HSP pending and PTW requests merge without duplicate returns", "[hsp-timing]")
{
  hsp_environment env;
  hsp_rig rig;
  rig.seed_hsp();
  const bool hit = GENERATE(true, false);
  const uint64_t address = hit ? 0x1000 : 0x3000;
  rig.issue(address, rig.upper);
  rig.tick(5);
  rig.issue(address, rig.other);
  rig.tick(10);
  if (!hit) {
    REQUIRE(rig.lower.RQ.size() == 1);
    auto pkt = rig.lower.RQ.front();
    rig.lower.RQ.pop_front();
    rig.lower.returned.emplace_back(pkt);
    rig.tick(4);
  }
  REQUIRE(rig.upper.returned.size() == 1);
  REQUIRE(rig.other.returned.size() == 1);
  rig.tick(20);
  CHECK(rig.lower.RQ.empty());
  CHECK(rig.upper.returned.size() == 1);
  CHECK(rig.other.returned.size() == 1);
}

TEST_CASE("HSP full credits backpressure without recounting tag accesses", "[hsp-timing]")
{
  hsp_environment env;
  hsp_rig rig;
  rig.issue(0x1000, rig.upper);
  rig.tick(5);
  rig.issue(0x2000, rig.upper);
  rig.tick(5);
  rig.issue(0x3000, rig.upper);
  rig.tick(20);
  CHECK(rig.lower.RQ.size() == 2);
  CHECK(rig.cache.sim_stats.bf_queries == 2);
  CHECK(rig.cache.sim_stats.hsp_queue_full > 0);
  uint64_t accesses = 0;
  for (const auto& [key, row] : page_stats::snapshot()) accesses += row.stlb_acc;
  CHECK(accesses == 3);
  auto pkt = rig.lower.RQ.front();
  rig.lower.RQ.pop_front();
  rig.lower.returned.emplace_back(pkt);
  rig.tick(20);
  CHECK(rig.cache.sim_stats.ptw_requests_issued == 3);
}

TEST_CASE("HSP warmup skips added search latency and drains across ROI boundary", "[hsp-timing]")
{
  hsp_environment env;
  hsp_rig rig;
  rig.seed_hsp();
  rig.issue(0x1000, rig.upper);
  rig.tick(5);
  rig.cache.begin_phase();
  rig.tick(8);
  CHECK(rig.upper.returned.size() == 1);
  rig.upper.returned.clear();
  rig.cache.warmup = true;
  rig.issue(0x2000, rig.upper);
  rig.tick(2);
  CHECK(rig.upper.returned.size() == 1);
}

TEST_CASE("HSP merges with an already issued PTW", "[hsp-timing]")
{
  hsp_environment env;
  hsp_rig rig;
  rig.issue(0x1000, rig.upper);
  rig.tick(5);
  REQUIRE(rig.lower.RQ.size() == 1);
  rig.issue(0x1000, rig.other);
  rig.tick(5);
  CHECK(rig.lower.RQ.size() == 1);
  CHECK(rig.cache.sim_stats.bf_queries == 1);
  rig.lower.returned.emplace_back(rig.lower.RQ.front());
  rig.lower.RQ.clear();
  rig.tick(10);
  CHECK(rig.upper.returned.size() == 1);
  CHECK(rig.other.returned.size() == 1);
}

TEST_CASE("HSP latency switch retains the immediate legacy hit path", "[hsp-timing]")
{
  hsp_environment env;
  env.set("CHAMPSIM_STLB_HSP_INTERNAL_LATENCY", "0");
  hsp_rig rig;
  rig.seed_hsp();
  rig.issue(0x1000, rig.upper);
  rig.tick(3);
  CHECK(rig.upper.returned.size() == 1);
  CHECK(rig.cache.sim_stats.hsp_timed_hits == 0);
}

TEST_CASE("HSP downstream backpressure does not restart the BF lookup", "[hsp-timing]")
{
  hsp_environment env;
  hsp_rig rig;
  rig.lower = champsim::channel{1, 1, 1, champsim::data::bits{12}, false};
  champsim::channel::request_type blocker{};
  blocker.address = champsim::address{0x9000};
  REQUIRE(rig.lower.add_rq(blocker));
  rig.issue(0x1000, rig.upper);
  rig.tick(20);
  CHECK(rig.cache.sim_stats.bf_queries == 1);
  CHECK(rig.cache.sim_stats.ptw_requests_issued == 0);
  rig.lower.RQ.clear();
  rig.tick();
  CHECK(rig.cache.sim_stats.bf_queries == 1);
  REQUIRE(rig.lower.RQ.size() == 1);
  CHECK(rig.lower.RQ.front().address == champsim::address{0x1000});
}

TEST_CASE("HSP search retains its data if the sampled slot is evicted", "[hsp-timing]")
{
  hsp_environment env;
  env.set("CHAMPSIM_STLB_HSP_BUFFER_CAPACITY", "1");
  hsp_rig rig;
  rig.seed_hsp();
  rig.issue(0x3000, rig.other);
  rig.tick(13);
  REQUIRE(rig.lower.RQ.size() == 1);
  rig.issue(0x1000, rig.upper);
  rig.tick(5);
  // C fills while A's translation is sampled but its HSP search has not completed.
  rig.lower.returned.emplace_back(rig.lower.RQ.front());
  rig.lower.RQ.clear();
  rig.tick(8);
  REQUIRE(rig.upper.returned.size() == 1);
  CHECK(rig.upper.returned.front().data == champsim::address{0x101000});
  CHECK(rig.other.returned.size() == 1);
  CHECK(rig.lower.RQ.empty());
}

TEST_CASE("Compiled HSP hotness width caps counters before halving", "[hsp-timing][counter-width]")
{
  hsp_environment env;
  env.set("CHAMPSIM_STLB_HSP_BUFFER_CAPACITY", "512");
  env.set("CHAMPSIM_STLB_HSP_BF_COUNTERS", "4912");
  env.set("CHAMPSIM_STLB_HSP_BF_HASHES", "7");
  env.set("CHAMPSIM_STLB_HSP_HOTNESS_SAT", "512");
  hsp_rig rig;
  const uint64_t limit = CACHE::hsp_counter_bits() == 4 ? 15 : 512;
  CHECK(CACHE::hsp_hotness_limit() == limit);
  for (uint64_t page = 1; page <= 386; ++page) rig.fill(page * 4096);
  for (int i = 0; i < 80; ++i) {
    rig.issue((i % 2 == 0 ? 1 : 386) * 4096, rig.upper);
    rig.tick(16);
    REQUIRE(rig.upper.returned.size() == 1);
    rig.upper.returned.clear();
    REQUIRE(rig.lower.RQ.empty());
  }
  for (uint64_t page = 2; page <= 385; ++page) {
    rig.issue(page * 4096, rig.upper);
    rig.tick(16);
    REQUIRE(rig.upper.returned.size() == 1);
    rig.upper.returned.clear();
    REQUIRE(rig.lower.RQ.empty());
  }
  const auto& snapshots = CACHE::get_hsp_halve_history().at("test_STLB");
  REQUIRE(snapshots.size() == 1);
  const auto& values = snapshots.back().entry_counters;
  REQUIRE(!values.empty());
  CHECK(*std::max_element(values.begin(), values.end()) == std::min<uint64_t>(40, limit) / 2);
}

TEST_CASE("Internal HSP pipeline reports BF false negatives after clipping", "[hsp-timing][counter-width]")
{
  hsp_environment env;
  env.set("CHAMPSIM_STLB_HSP_BUFFER_CAPACITY", "512");
  hsp_rig rig;
  const uint64_t maximum = (uint64_t{1} << CACHE::bf_counter_bits()) - 1;
  for (uint64_t page = 1; page <= maximum + 2; ++page) rig.fill(page * 4096);
  REQUIRE(rig.cache.sim_stats.bf_counter_saturations == 1);
  rig.cache.begin_phase();
  uint64_t stlb_page = maximum + 2;
  for (uint64_t page = 1; page <= maximum; ++page) {
    rig.cache.invalidate_entry(champsim::address{stlb_page * 4096});
    rig.issue(page * 4096, rig.upper);
    rig.tick(16);
    REQUIRE(rig.upper.returned.size() == 1);
    REQUIRE(rig.lower.RQ.empty());
    rig.upper.returned.clear();
    stlb_page = page;
  }
  rig.issue((maximum + 1) * 4096, rig.upper);
  rig.tick(16);
  CHECK(rig.upper.returned.empty());
  CHECK(rig.lower.RQ.size() == 1);
  CHECK(rig.cache.sim_stats.bf_false_negatives == 1);
  CHECK(rig.cache.sim_stats.bf_false_positives == 0);
  CHECK(rig.cache.sim_stats.bf_false_positive_queries == 0);
}
