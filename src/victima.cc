#include "cache.h"
#include <algorithm>
#include <cassert>
using champsim::translation_op;

CACHE::page_key CACHE::victima_key(const request_type& r)
{ return {r.cpu,r.asid[0],r.asid[1],r.address.to<uint64_t>() >> 12}; }

bool CACHE::victima_admit(const request_type& r) const
{
  if (champsim::victima_mpki(champsim::victima_data_misses,r.cpu) >= 5) return true;
  auto it=victima_history.find(victima_key(r));
  // Initial comparator policy; explicit tunable approximation to the trained paper tree.
  return it!=victima_history.end() && (it->second.frequency>=2 || it->second.cost>=2);
}

bool CACHE::victima_start(const tag_lookup_type& r)
{
  auto [m,req]=mshr_and_forward_packet(r);
  for (auto& old : MSHR) {
    if (old.cpu==r.cpu && old.asid[0]==r.asid[0] && old.asid[1]==r.asid[1] && champsim::page_number{old.address}==champsim::page_number{r.address}) {
      old=mshr_type::merge(old,m); ++sim_stats.victima_merge; return true;
    }
  }
  if (MSHR.size()>=MSHR_SIZE || victima_pending_requests.size()>=MSHR_SIZE+16) return false;
  auto id=victima_next_id++;
  req.translation.id=id; req.translation.op=translation_op::walk;
  m.translation=req.translation;
  victima_pending pending{};
  pending.request = req;
  victima_pending_requests.emplace(id, pending);
  MSHR.push_back(m);
  ++champsim::victima_stlb_misses[r.cpu];
  sim_stats.misses.increment({r.type,r.cpu});
  return true;
}

void CACHE::victima_evict(const champsim::cache_block& b)
{
  request_type r; r.cpu=b.owner; r.asid[0]=b.asid[0]; r.asid[1]=b.asid[1];
  r.address=b.address; r.v_address=b.address; r.type=access_type::TRANSLATION;
  if (!victima_admit(r)) return;
  for (const auto& [id,p]:victima_pending_requests) {
    if (p.request.cpu==r.cpu && p.request.asid[0]==r.asid[0] && p.request.asid[1]==r.asid[1] && (p.request.address.to<uint64_t>()>>15)==(r.address.to<uint64_t>()>>15)) {
      ++sim_stats.victima_merge; return;
    }
  }
  auto backgrounds=std::count_if(victima_pending_requests.begin(),victima_pending_requests.end(),[](const auto& p){return p.second.background;});
  if (backgrounds>=16 || victima_pending_requests.size()>=MSHR_SIZE+16) {++sim_stats.victima_queue_drop; return;}
  auto id=victima_next_id++; r.translation.id=id; r.translation.op=translation_op::walk;
  victima_pending p{}; p.request=r; p.background=true;
  victima_pending_requests.emplace(id,p);
}

void CACHE::victima_receive(const response_type& r)
{
  auto it=victima_pending_requests.find(r.translation.id);
  assert(it!=victima_pending_requests.end());
  auto& p=it->second;
  if (r.translation.op==translation_op::walk) {
    assert(!p.walk_done); p.walk_done=true; p.walk_result=r;
    auto& h=victima_history[victima_key(p.request)];
    h.frequency=static_cast<uint8_t>(std::min(7,int(h.frequency)+1));
    h.cost=static_cast<uint8_t>(std::min(15,int(h.cost)+int(r.dram_access)));
    if (p.completed) ++sim_stats.victima_stale_result;
  } else {
    assert(!p.lookup_done); p.lookup_done=true; p.lookup_hit=r.translation.hit;
  }
  bool usable = r.translation.op==translation_op::walk || r.translation.hit;
  if (!p.background && !p.completed && usable) {
    auto m=std::find_if(MSHR.begin(),MSHR.end(),[&](const auto& x){return x.translation.id==it->first;});
    assert(m!=MSHR.end());
    auto first=std::find_if(MSHR.begin(),MSHR.end(),[](const auto& x){return x.data_promise.has_unknown_readiness();});
    m->data_promise=champsim::waitable{mshr_type::returned_value{r.data,r.pf_metadata},current_time+(warmup?champsim::chrono::clock::duration{}:FILL_LATENCY)};
    std::iter_swap(m,first);
    p.completed=true; ++sim_stats.victima_completion;
  }
}

void CACHE::victima_tick()
{
  for (const auto& r:victima_channel->returned) victima_receive(r);
  victima_channel->returned.clear();
  for (auto it=victima_pending_requests.begin();it!=victima_pending_requests.end();) {
    auto& p=it->second;
    if (!p.lookup_sent) {
      auto r=p.request; r.translation.op=p.background?translation_op::probe:translation_op::lookup;
      p.lookup_sent=victima_channel->add_rq(r);
      if (!p.lookup_sent) ++sim_stats.victima_retry;
    }
    if (!p.walk_sent && (!p.background || (p.lookup_done && !p.lookup_hit))) {
      p.walk_sent=lower_level->add_rq(p.request);
      if (p.walk_sent && p.background) ++sim_stats.victima_background_walk;
      if (!p.walk_sent) ++sim_stats.victima_retry;
    }
    if (p.background && p.lookup_done && p.lookup_hit) {it=victima_pending_requests.erase(it); continue;}
    if (p.lookup_done && p.walk_done) {
      if (!p.lookup_hit && victima_admit(p.request)) {
        auto r=p.request; r.response_requested=false;
        r.translation=p.walk_result->translation;
        r.translation.op=p.background?translation_op::insert_background:translation_op::insert_demand;
        if (!victima_channel->add_rq(r)) {++sim_stats.victima_retry; ++it; continue;}
      }
      it=victima_pending_requests.erase(it);
    } else ++it;
  }
}

std::vector<champsim::cache_block>::iterator CACHE::victima_victim(set_type::iterator begin, set_type::iterator end, uint32_t owner)
{
  auto invalid=std::find_if(begin,end,[](const auto& b){return !b.valid;});
  if(invalid!=end) return invalid;
  bool pressure=champsim::victima_mpki(champsim::victima_stlb_misses,owner)>5;
  for (;;) {
    auto first=std::find_if(begin,end,[](const auto& b){return b.rrpv==3;});
    if(first!=end) {
      if(first->translation && pressure) {
        auto data=std::find_if(begin,end,[](const auto& b){return !b.translation && b.rrpv==3;});
        if(data!=end) return data;
      }
      return first;
    }
    for(auto b=begin;b!=end;++b) b->rrpv=static_cast<uint8_t>(std::min(3,int(b->rrpv)+1));
  }
}

bool CACHE::victima_access(const tag_lookup_type& r)
{
  if (!victima_l2 || PAGE_SIZE!=4096 || BLOCK_SIZE!=64) throw std::invalid_argument("Victima requires enabled L2, 4KB pages and 64B lines");
  auto cluster=r.address.to<uint64_t>()>>15;
  auto slot=(r.address.to<uint64_t>()>>12)&7;
  auto index_address=champsim::address{cluster << champsim::to_underlying(OFFSET_BITS)};
  auto [begin,end]=get_set_span(index_address);
  auto found=std::find_if(begin,end,[&](const auto& b){return b.valid && b.translation && b.translations.cluster==cluster && b.translations.cpu==r.cpu && b.translations.asid==std::array<uint8_t,2>{r.asid[0],r.asid[1]} && b.translations.page_bits==12;});
  bool insert=r.translation.op==translation_op::insert_demand || r.translation.op==translation_op::insert_background;
  if (!insert) {
    bool hit=found!=end && (r.translation.op==translation_op::probe || found->translations.valid[slot]);
    if(hit) {++sim_stats.victima_lookup_hit; found->rrpv=static_cast<uint8_t>(std::max(0,int(found->rrpv)-(champsim::victima_mpki(champsim::victima_stlb_misses,r.cpu)>5?3:1)));}
    else ++sim_stats.victima_lookup_miss;
    response_type response{r.address,r.v_address,hit?found->translations.ppn[slot]:champsim::address{},0,r.instr_depend_on_me};
    response.translation=r.translation; response.translation.hit=hit;
    response.cpu=r.cpu; response.asid={r.asid[0],r.asid[1]};
    for(auto* q:r.to_return) q->push_back(response);
    return true; // explicit miss response, NEVER forward to LLC
  }
  if (victima_fill_budget == 0) return false;
  if(found!=end) {
    --victima_fill_budget;
    for(unsigned i=0;i<8;++i) if(r.translation.entries.valid[i]) {found->translations.valid[i]=true;found->translations.ppn[i]=r.translation.entries.ppn[i];}
    ++sim_stats.victima_merge; return true;
  }
  auto victim=victima_victim(begin,end,r.cpu);
  if(victim->valid && !victim->translation && victim->dirty) {
    request_type wb; wb.address=victim->address; wb.data=victim->data; wb.cpu=r.cpu; wb.type=access_type::WRITE; wb.response_requested=false;
    if(!lower_level->add_wq(wb)) {++sim_stats.victima_retry; return false;}
  }
  // Remove the clean physical PTE copy only after the target replacement can commit.
  auto [pb,pe]=get_set_span(r.translation.entries.pte_address);
  for(auto b=pb;b!=pe;++b) if(b!=victim && b->valid && !b->translation && !b->dirty && champsim::block_number{b->address}==champsim::block_number{r.translation.entries.pte_address}) b->valid=false;
  if(victim->valid && victim->translation) ++sim_stats.victima_translation_eviction;
  else {++sim_stats.victima_occupancy; if(victim->valid) ++sim_stats.victima_data_eviction;}
  --victima_fill_budget;
  *victim=BLOCK{}; victim->valid=true; victim->translation=true; victim->translations=r.translation.entries; victim->address=index_address;
  victim->rrpv=champsim::victima_mpki(champsim::victima_stlb_misses,r.cpu)>5?0:3;
  if(r.translation.op==translation_op::insert_demand) ++sim_stats.victima_demand_insert; else ++sim_stats.victima_background_insert;
  return true;
}
