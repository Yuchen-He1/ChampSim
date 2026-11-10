#include <catch.hpp>
#include "cache.h"
#include "defaults.hpp"
#include "dram_controller.h"
#include "ptw.h"
#include "vmem.h"

namespace {
using request = champsim::channel::request_type;
using response = champsim::channel::response_type;
using op = champsim::translation_op;
void tick(CACHE& c, int n=32) { for(int i=0;i<n;++i) c._operate(); }
request lookup(uint64_t va, uint64_t id=1) {
  request r; r.address=champsim::address{va}; r.v_address=r.address; r.cpu=0;
  r.translation.op=op::lookup; r.translation.id=id; return r;
}
request insertion(uint64_t va) {
  auto r=lookup(va); r.translation.op=op::insert_demand; r.response_requested=false;
  r.translation.entries.cluster=va>>15; r.translation.entries.cpu=0;
  r.translation.entries.pte_address=champsim::address{0x300000};
  for(unsigned i=0;i<8;++i) {r.translation.entries.valid[i]=true; r.translation.entries.ppn[i]=champsim::address{0x100000+((i*13)%19)*4096};}
  return r;
}
auto l2_builder(champsim::channel& ul, champsim::channel& ll) {
  return champsim::cache_builder{}.name("L2").sets(2).ways(2).mshr_size(8).hit_latency(3).fill_latency(2)
    .tag_bandwidth(champsim::bandwidth::maximum_type{1}).fill_bandwidth(champsim::bandwidth::maximum_type{1})
    .upper_levels({&ul}).lower_level(&ll).victima_l2(true);
}
}

TEST_CASE("Victima stores eight real mappings, isolates namespaces, and returns misses locally", "[victima]") {
  champsim::channel ul,ll;
  CACHE c{l2_builder(ul,ll)}; c.warmup=false; c.initialize();
  auto ins=insertion(0x80000); ins.translation.entries.valid[3]=false;
  REQUIRE(ul.add_rq(ins)); tick(c);
  REQUIRE(c.sim_stats.victima_occupancy==1);
  for(unsigned i=0;i<8;++i) REQUIRE(ul.add_rq(lookup(0x80000+i*4096,i+1)));
  tick(c,100);
  REQUIRE(ul.returned.size()==8);
  for(unsigned i=0;i<8;++i) {
    REQUIRE(ul.returned[i].translation.hit==(i!=3));
    if(i!=3) REQUIRE(ul.returned[i].data==ins.translation.entries.ppn[i]);
  }
  REQUIRE(ll.RQ.empty()); ul.returned.clear();
  auto other=lookup(0x80000,10); other.asid[0]=1;
  REQUIRE(ul.add_rq(other)); tick(c);
  REQUIRE_FALSE(ul.returned.back().translation.hit); REQUIRE(ll.RQ.empty());
  auto data=lookup((0x80000>>15)<<6); data.translation={};
  REQUIRE(ul.add_rq(data)); tick(c);
  REQUIRE(ll.RQ.size()==1); // numerical tag collision cannot hit translation block
}

TEST_CASE("Victima races complete once and retry both output queues", "[victima]") {
  champsim::channel ul;
  champsim::channel walk{1,0,0,champsim::data::bits{12},false};
  champsim::channel l2{1,0,0,champsim::data::bits{6},false};
  CACHE stlb{champsim::cache_builder{}.name("STLB").sets(1).ways(1).mshr_size(4).hit_latency(2).fill_latency(2)
    .tag_bandwidth(champsim::bandwidth::maximum_type{1}).fill_bandwidth(champsim::bandwidth::maximum_type{1})
    .offset_bits(champsim::data::bits{12}).upper_levels({&ul}).lower_level(&walk).victima_channel(&l2)};
  stlb.warmup=false; stlb.initialize();
  auto req=lookup(0x80000); req.translation={};
  REQUIRE(walk.add_rq(req)); REQUIRE(l2.add_rq(req)); // force backpressure
  REQUIRE(ul.add_rq(req)); tick(stlb);
  REQUIRE(ul.returned.empty()); REQUIRE(stlb.sim_stats.victima_retry>0);
  walk.RQ.clear(); l2.RQ.clear(); tick(stlb);
  REQUIRE(walk.RQ.size()==1); REQUIRE(l2.RQ.size()==1);
  response wr{walk.RQ.front()}, lr{l2.RQ.front()}; walk.RQ.clear(); l2.RQ.clear();
  wr.data=champsim::address{0x987000}; lr.data=wr.data; lr.translation.hit=true;
  SECTION("L2 wins") {
    l2.returned.push_back(lr); tick(stlb); REQUIRE(ul.returned.size()==1);
    walk.returned.push_back(wr); tick(stlb); REQUIRE(stlb.sim_stats.victima_stale_result==1);
  }
  SECTION("PTW wins") {
    walk.returned.push_back(wr); tick(stlb); REQUIRE(ul.returned.size()==1);
    l2.returned.push_back(lr); tick(stlb);
  }
  REQUIRE(ul.returned.size()==1); REQUIRE(ul.returned.front().data==wr.data);
  REQUIRE(stlb.sim_stats.victima_completion==1); REQUIRE(stlb.MSHR.empty());
}

TEST_CASE("Victima dirty data eviction retries without losing data or duplicating translation blocks", "[victima]") {
  champsim::channel ul; champsim::channel ll{8,0,1,champsim::data::bits{6},false};
  CACHE c{l2_builder(ul,ll).sets(1).ways(1)}; c.warmup=false; c.initialize();
  c.block[0].valid=true; c.block[0].dirty=true; c.block[0].address=champsim::address{0x40000};
  request occupied; REQUIRE(ll.add_wq(occupied));
  auto ins=insertion(0x80000); REQUIRE(ul.add_rq(ins)); tick(c);
  REQUIRE_FALSE(c.block[0].translation); REQUIRE(c.block[0].dirty);
  ll.WQ.clear(); tick(c); REQUIRE(c.block[0].translation);
  REQUIRE(ll.WQ.size()==1); REQUIRE(ll.WQ.front().address==champsim::address{0x40000});
  REQUIRE(ul.add_rq(ins)); tick(c);
  REQUIRE(c.sim_stats.victima_occupancy==1); REQUIRE(c.sim_stats.victima_merge==1);
  ll.WQ.clear(); REQUIRE(ul.add_rq(insertion(0x100000))); tick(c);
  REQUIRE(ll.WQ.empty()); // translation eviction never generates dirty writeback
}

TEST_CASE("Victima VMEM snapshot never allocates neighbouring pages and PTW returns it", "[victima]") {
  MEMORY_CONTROLLER dram{champsim::chrono::picoseconds{3200},champsim::chrono::picoseconds{6400},18,18,18,38,
    champsim::chrono::microseconds{64000},{},64,64,1,champsim::data::bytes{8},1024,1024,4,4,4,8192};
  VirtualMemory vm{champsim::data::bytes{4096},5,champsim::chrono::nanoseconds{0},dram,42};
  auto pa=vm.va_to_pa(0,champsim::page_number{0x80}).first;
  auto pb=vm.va_to_pa(0,champsim::page_number{0x85}).first;
  auto free=vm.available_ppages();
  auto entries=vm.cluster(0,champsim::address{0x80000},255,255);
  REQUIRE(vm.available_ppages()==free); REQUIRE(entries.valid[0]); REQUIRE(entries.valid[5]);
  REQUIRE_FALSE(entries.valid[1]); REQUIRE(entries.ppn[0]==champsim::address{pa}); REQUIRE(entries.ppn[5]==champsim::address{pb});
  auto ns=vm.domain(0,1,255); auto pc=vm.va_to_pa(ns,champsim::page_number{0x80}).first;
  REQUIRE(pc!=pa); REQUIRE(vm.cluster(0,champsim::address{0x80000},1,255).ppn[0]==champsim::address{pc});
  champsim::channel ul,ll;
  PageTableWalker ptw{champsim::ptw_builder{champsim::defaults::default_ptw}.upper_levels({&ul}).lower_level(&ll).virtual_memory(&vm)};
  ptw.warmup=false;
  auto r=lookup(0x80000,77); r.translation.op=op::walk; REQUIRE(ul.add_rq(r));
  int steps=0;
  for(int i=0;i<1000;++i) {
    ptw._operate();
    for(const auto& p:ll.RQ) {ll.returned.emplace_back(p); ++steps;}
    ll.RQ.clear();
  }
  REQUIRE(steps==5); REQUIRE(ul.returned.size()==1);
  REQUIRE(ul.returned.front().translation.id==77);
  REQUIRE(ul.returned.front().translation.entries.ppn[5]==champsim::address{pb});
  REQUIRE_FALSE(ul.returned.front().translation.entries.valid[1]);
}

TEST_CASE("Victima eviction schedules an independent background walk and never refills STLB", "[victima]") {
  champsim::victima_retired[0]=1000; champsim::victima_data_misses[0]=10;
  champsim::channel ul,walk,l2;
  CACHE stlb{champsim::cache_builder{}.name("STLB").sets(1).ways(1).mshr_size(4).hit_latency(2).fill_latency(2)
    .tag_bandwidth(champsim::bandwidth::maximum_type{1}).fill_bandwidth(champsim::bandwidth::maximum_type{1})
    .offset_bits(champsim::data::bits{12}).upper_levels({&ul}).lower_level(&walk).victima_channel(&l2)};
  stlb.warmup=false; stlb.initialize();
  auto demand=[&](uint64_t va) {
    auto r=lookup(va); r.translation={}; REQUIRE(ul.add_rq(r)); tick(stlb);
    REQUIRE(walk.RQ.size()==1); REQUIRE(l2.RQ.size()==1);
    response wr{walk.RQ.front()},lr{l2.RQ.front()}; walk.RQ.clear(); l2.RQ.clear();
    wr.data=champsim::address{va+0x100000};
    wr.translation.entries=insertion(va).translation.entries;
    lr.translation.hit=false;
    walk.returned.push_back(wr); l2.returned.push_back(lr); tick(stlb);
  };
  demand(0x80000);
  REQUIRE(ul.returned.size()==1); REQUIRE(l2.RQ.size()==1);
  REQUIRE(l2.RQ.front().translation.op==op::insert_demand); l2.RQ.clear();
  demand(0x100000);
  REQUIRE(ul.returned.size()==2);
  auto probe=std::find_if(l2.RQ.begin(),l2.RQ.end(),[](const auto& r){return r.translation.op==op::probe;});
  REQUIRE(probe!=l2.RQ.end()); REQUIRE(probe->address==champsim::address{0x80000});
  response pr{*probe}; pr.translation.hit=false; l2.RQ.clear(); l2.returned.push_back(pr);
  tick(stlb); REQUIRE(walk.RQ.size()==1);
  response bg{walk.RQ.front()}; bg.translation.entries=insertion(0x80000).translation.entries; walk.RQ.clear();
  walk.returned.push_back(bg); tick(stlb);
  REQUIRE(stlb.sim_stats.victima_background_walk==1);
  REQUIRE(ul.returned.size()==2); REQUIRE(stlb.block[0].address==champsim::address{0x100000});
  REQUIRE(l2.RQ.size()==1); REQUIRE(l2.RQ.front().translation.op==op::insert_background);
  champsim::victima_retired.clear(); champsim::victima_data_misses.clear(); champsim::victima_stlb_misses.clear();
}
