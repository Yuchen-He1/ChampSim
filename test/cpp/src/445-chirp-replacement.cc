#include <catch.hpp>

#include <array>

#include "../replacement/chirp/chirp.h"
#include "control_flow_history.h"
#include "instruction.h"
#include "ooo_cpu.h"

namespace
{
void lookup(chirp& policy, long way, uint64_t address, uint64_t pc, bool hit, uint32_t cpu = 0)
{
  policy.update_replacement_state(cpu, 0, way, champsim::address{address}, champsim::address{pc}, {}, access_type::TRANSLATION, hit);
}
void fill(chirp& policy, long way, uint64_t address, uint64_t pc, uint32_t cpu = 0)
{
  policy.replacement_cache_fill(cpu, 0, way, champsim::address{address}, champsim::address{pc}, {}, access_type::TRANSLATION);
}
} // namespace

TEST_CASE("CHiRP selects invalid ways, then first dead way, then LRU", "[chirp]")
{
  chirp policy{nullptr, 2, 4};
  std::array<champsim::cache_block, 4> ways{};
  auto victim = [&]() { return policy.find_victim(0, 0, 1, ways.data(), {}, {}, access_type::TRANSLATION); };
  for (auto& way : ways)
    way.valid = true;
  for (long way = 0; way < 4; ++way)
    policy.get_entry(1, way).last_used = 4 - way;
  CHECK(victim() == 3);
  policy.get_entry(1, 1).dead = true;
  policy.get_entry(1, 2).dead = true;
  CHECK(victim() == 1);
  ways[2].valid = false;
  CHECK(victim() == 2);
}

TEST_CASE("CHiRP trains live LRU evictions and suppresses dead eviction training", "[chirp]")
{
  champsim::cpu_control_flow_history.clear();
  chirp policy{nullptr, 1, 1};
  policy.prediction_table.assign(chirp::TABLE_SIZE, 0);
  fill(policy, 0, 0x1000, 0x100);
  auto old_signature = policy.get_entry(0, 0).signature;
  CHECK(policy.counter(old_signature) == 0); // invalid initial entry did not train
  fill(policy, 0, 0x2000, 0x200);
  CHECK(policy.counter(old_signature) == 1);
  auto& line = policy.get_entry(0, 0);
  line.dead = true;
  line.signature = old_signature;
  fill(policy, 0, 0x3000, 0x300);
  CHECK(policy.counter(old_signature) == 1);
  policy.get_entry(0, 0).signature = old_signature;
  policy.get_entry(0, 0).dead = false;
  policy.counter(old_signature) = chirp::COUNTER_MAX;
  fill(policy, 0, 0x4000, 0x400);
  CHECK(policy.counter(old_signature) == chirp::COUNTER_MAX);
}

TEST_CASE("CHiRP only trains and predicts on the first hit", "[chirp]")
{
  champsim::cpu_control_flow_history.clear();
  chirp policy{nullptr, 1, 1};
  fill(policy, 0, 0x1000, 0x100);
  auto old_signature = policy.get_entry(0, 0).signature;
  policy.counter(old_signature) = 2;
  auto new_signature = chirp::hash_signature((0x204 >> 2) ^ policy.path_history[0]);
  policy.counter(new_signature) = 3;
  lookup(policy, 0, 0x1000, 0x204, true);
  CHECK(policy.counter(old_signature) == 1);
  CHECK(policy.get_entry(0, 0).dead);
  CHECK_FALSE(policy.get_entry(0, 0).first_hit);
  auto table = policy.prediction_table;
  auto signature = policy.get_entry(0, 0).signature;
  lookup(policy, 0, 0x1000, 0x308, true);
  CHECK(policy.prediction_table == table);
  CHECK(policy.get_entry(0, 0).dead);
  CHECK(policy.get_entry(0, 0).signature != signature);
}

TEST_CASE("CHiRP live training saturates at zero", "[chirp]")
{
  champsim::cpu_control_flow_history.clear();
  chirp policy{nullptr, 1, 1};
  fill(policy, 0, 0x1000, 0x100);
  auto signature = policy.get_entry(0, 0).signature;
  lookup(policy, 0, 0x1000, 0x200, true);
  CHECK(policy.counter(signature) == 0);
}

TEST_CASE("CHiRP uses a strict dead threshold and reads before alias training", "[chirp]")
{
  champsim::cpu_control_flow_history.clear();
  chirp policy{nullptr, 1, 1};
  auto signature = chirp::hash_signature(0x100 >> 2);
  policy.counter(signature) = chirp::DEAD_THRESHOLD;
  fill(policy, 0, 0x1000, 0x100);
  CHECK_FALSE(policy.get_entry(0, 0).dead);
  // Force old and incoming signatures to alias the same counter.
  policy.path_history[0] = 0;
  fill(policy, 0, 0x2000, 0x100);
  CHECK(policy.counter(signature) == chirp::DEAD_THRESHOLD + 1);
  CHECK_FALSE(policy.get_entry(0, 0).dead);
  policy.path_history[0] = 0;
  fill(policy, 0, 0x3000, 0x100);
  CHECK(policy.get_entry(0, 0).dead);
}

TEST_CASE("CHiRP keeps lookup signatures across delayed fills and retries", "[chirp]")
{
  champsim::cpu_control_flow_history.clear();
  chirp policy{nullptr, 1, 2};
  lookup(policy, 2, 0x1000, 0x104, false);
  auto signature = policy.pending_signatures.at({0, 0x1000});
  auto path = policy.path_history[0];
  lookup(policy, 2, 0x1000, 0x104, false);
  CHECK(policy.path_history[0] == path);
  lookup(policy, 2, 0x2000, 0x208, false);
  champsim::cpu_control_flow_history[0].observe(0x1230, true, false);
  fill(policy, 0, 0x1000, 0x104);
  CHECK(policy.get_entry(0, 0).signature == signature);
  CHECK(policy.pending_signatures.count({0, 0x1000}) == 0);
  CHECK(policy.pending_signatures.count({0, 0x2000}) == 1);
}

TEST_CASE("CHiRP preserves LRU on hits and fills without touching other sets", "[chirp]")
{
  champsim::cpu_control_flow_history.clear();
  chirp policy{nullptr, 2, 2};
  std::array<champsim::cache_block, 2> ways{};
  for (auto& way : ways)
    way.valid = true;
  fill(policy, 0, 0x1000, 0x100);
  fill(policy, 1, 0x2000, 0x200);
  lookup(policy, 0, 0x1000, 0x100, true);
  CHECK(policy.find_victim(0, 0, 0, ways.data(), {}, {}, access_type::TRANSLATION) == 1);
  CHECK_FALSE(policy.get_entry(1, 0).valid);
  CHECK(policy.get_entry(1, 0).last_used == 0);
}

TEST_CASE("CHiRP branch and path histories preserve PCs and CPU isolation", "[chirp]")
{
  champsim::cpu_control_flow_history.clear();
  auto& history = champsim::cpu_control_flow_history[0];
  history.observe(0x1230, true, false);
  history.observe(0x4560, false, true);
  history.observe(0x7890, true, false);
  CHECK(history.conditional == 0x2389);
  CHECK(history.indirect == 0x56);
  history.observe(0xffff, false, false);
  CHECK(history.conditional == 0x2389);
  CHECK(history.indirect == 0x56);
  chirp policy{nullptr, 1, 1};
  CHECK(policy.access_signature(0, champsim::address{0x104}) == chirp::hash_signature((0x104 >> 2) ^ 0x2389 ^ 0x56));
  CHECK(policy.path_history[0] == 1);
  policy.access_signature(0, champsim::address{0x108});
  CHECK(policy.path_history[0] == 0x12);
  CHECK(policy.access_signature(1, champsim::address{0x104}) == chirp::hash_signature(0x104 >> 2));
  for (int i = 0; i < 16; ++i)
    policy.access_signature(0, champsim::address{0x100});
  CHECK(policy.path_history[0] == 0);
  for (int i = 0; i < 8; ++i)
    history.observe(0, true, true);
  CHECK(history.conditional == 0);
  CHECK(history.indirect == 0);
}

TEST_CASE("CPU initialization resets branch history", "[chirp]")
{
  O3_CPU cpu{champsim::core_builder{}};
  champsim::cpu_control_flow_history[cpu.cpu] = {0xffff, 0xffff};
  cpu.initialize();
  CHECK(champsim::cpu_control_flow_history[cpu.cpu].conditional == 0);
  CHECK(champsim::cpu_control_flow_history[cpu.cpu].indirect == 0);
}

TEST_CASE("The CPU supplies conditional and unconditional indirect branch PCs", "[chirp]")
{
  champsim::cpu_control_flow_history.clear();
  CACHE l1i{champsim::cache_builder{}.sets(1).ways(1)};
  O3_CPU cpu{champsim::core_builder{}.l1i(&l1i)};
  cpu.initialize();
  for (auto type : {BRANCH_CONDITIONAL, BRANCH_DIRECT_JUMP, BRANCH_INDIRECT, BRANCH_DIRECT_CALL, BRANCH_INDIRECT_CALL, BRANCH_RETURN}) {
    ooo_model_instr instruction{0, input_instr{}};
    instruction.ip = champsim::address{0x1230};
    instruction.is_branch = true;
    instruction.branch = type;
    cpu.do_predict_branch(instruction);
  }
  CHECK(champsim::cpu_control_flow_history[cpu.cpu].conditional == 0x23);
  CHECK(champsim::cpu_control_flow_history[cpu.cpu].indirect == 0x232323);
}

TEST_CASE("CHiRP uses translation granularity and clears cross-CPU merged misses", "[chirp]")
{
  champsim::cpu_control_flow_history.clear();
  CACHE cache{champsim::cache_builder{}.sets(1).ways(1).offset_bits(champsim::data::bits{12})};
  chirp policy{&cache};
  lookup(policy, 1, 0x1004, 0x104, false, 0);
  lookup(policy, 1, 0x1008, 0x108, false, 1);
  auto signature = policy.pending_signatures.at({0, 1});
  fill(policy, 0, 0x1000, 0x104, 0);
  CHECK(policy.get_entry(0, 0).signature == signature);
  CHECK(policy.pending_signatures.empty());
}

TEST_CASE("CHiRP does not train an invalidated translation as dead", "[chirp]")
{
  champsim::cpu_control_flow_history.clear();
  CACHE cache{champsim::cache_builder{}.sets(1).ways(1)};
  chirp policy{&cache};
  fill(policy, 0, 0x1000, 0x100);
  auto signature = policy.get_entry(0, 0).signature;
  cache.block[0].valid = false;
  fill(policy, 0, 0x2000, 0x200);
  CHECK(policy.counter(signature) == 0);
}
