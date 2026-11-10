# Victima native / 4 KB prototype

Based on local `default` (`ab9f6d38`). Enable with:

```sh
./config.sh victima.json
make -j4
bin/champsim_victima --warmup-instructions 100000 --simulation-instructions 1000000 --json results.json trace.champsimtrace.xz
```

The example is single-core. For multiple cores explicitly connect each STLB's
`victima_channel` to its corresponding L2 name and enable `victima_l2` there.
Victima is off unless these options are supplied. Only 4096-byte pages and
64-byte cache lines are supported; unsupported geometry raises an error.
No mapping changes, ASID reuse, shootdowns, nested translation, or huge pages
are modeled in this version. Do not use it for experiments requiring those.

## Mechanism

* `translation.h` defines separate request operations and eight physical page
  bases with individual valid bits, cluster, CPU, ASID and page-size tags.
  Translation blocks occupy ordinary L2 ways; numerical data tags cannot hit them.
* `victima.cc` coordinates two independent queue submissions for each STLB miss.
  Admission never gates either demand walk or lookup. Each submission retries
  backpressure. The first usable response fills the original STLB MSHR once.
  A late walk is drained and its persistent counters updated; it cannot complete
  a newer request to the same VPN. Walk traffic is **not canceled**, so this
  prototype models the permitted ignore-late-results variant, not PTW savings
  from early cancellation.
* Lookups/probes/inserts use L2's normal tag pipeline, latency and arbitration.
  Inserts additionally consume remaining fill bandwidth. A lookup miss returns
  a control response and never reaches the LLC. There is no cluster promotion
  into L1D.
* VMEM provides a read-only snapshot after the demand page is resolved at the
  final PTW memory response. Only existing neighbour mappings are included;
  missing neighbours remain invalid. It does not perform eight walks or allocate
  adjacent pages. PPNs need not be contiguous. Explicit ASIDs have separate VMEM
  namespaces; TLB tags, outstanding translations, channels and PSCL tags preserve
  address-space identity.
* Successful demand walks update a persistent per-page 3-bit saturating frequency
  counter and a 4-bit saturating cost counter. Cost increments if **any** step
  reaches DRAM, propagated through cache misses; cache hits do not inherit a
  previous request's DRAM flag. Background walks update these counters too.
* STLB eviction consults this history and saves the victim before replacement.
  Up to 16 background jobs run independently of future misses. A timed L2 probe
  first checks block presence, then issues a background walk only on absence.
  The result fills L2 only. Pending clusters are coalesced, optional background
  overflow is counted and dropped; demand requests are never dropped.
* Insertion uses the VPN-cluster destination set, handles a dirty data victim's
  writeback with retry, and removes a resident clean physical PTE source copy
  after the insertion can commit. A dirty source copy is retained. Translation
  victims are discarded without writeback. Duplicate cluster inserts merge valid
  mappings instead of allocating extra ways.
* L2 uses block-local RRIP: under STLB MPKI > 5, translation insertion gets 0,
  hits decrement by 3, and victim selection tries another eligible data way.
  Otherwise insertion gets 3 and hits decrement by 1. Counters saturate at 0/3.
  This overrides the configured replacement module for an enabled L2.

## Explicit modeling choices

The source of the mechanism is [the Victima paper, sections 5.1–5.3](https://arxiv.org/html/2310.04158v3).
This is not the artifact's tag-only translation-hit approximation. It is also
not a claim of reproducing published performance or the learned predictor tree.

The first-version comparator is `frequency >= 2 OR cost >= 2`. L2 data MPKI >= 5
bypasses that comparator. These comparator thresholds are an **approximation**,
not the paper's trained decision tree. MPKI uses cumulative retired instructions
and per-core misses (including warmup), rather than an adaptive sampling window.
Counters persist across STLB eviction and phase boundaries. Metadata is modeled
out of line without extra memory transfers. The PTE contents are a VMEM snapshot,
not bytes stored in the ordinary data-cache payload. Mapping validity reflects
this simulator's demand-created page mappings, not OS permissions.

For performance comparisons use a matching baseline geometry and record these
choices. Continuing the losing walk consumes memory resources and may understate
Victima's benefit. Initial page faults also affect timing.

## Statistics and checks

JSON cache statistics include `victima lookup_hit`, `lookup_miss`, `demand_insert`,
`background_insert`, `background_walk`, `merge`, `queue_drop`, `retry`,
`stale_result`, `completion`, `data_eviction`, `translation_eviction`, `data_miss`,
and `occupancy`. Lookup counts include background probes. Occupancy is an end-of-
phase gauge, not a counter difference. Retry counts measure attempts, not jobs.
Normal cache hit/miss and latency statistics remain available for baseline data
impact comparisons. Additional misses are not causally attributed to Victima
without a paired baseline run.

`make test` includes `[victima]` cases for all eight slots, invalid slots, ASID
isolation, data-tag collisions, two response orders, queue backpressure, dirty
replacement retry, duplicate insertion, translation eviction without writeback,
nonallocating VMEM snapshots, one-walk cluster responses, and independent eviction
walks. Runtime assertions reject missing IDs or duplicate backend responses.

## Validation of this revision

* Full C++ suite: 622 test cases, 10,937 assertions passed.
* Python configuration suite: 228 tests, one skipped, passed.
* Synthetic 4,096-page workload: 10,000 warmup + 50,000 measured instructions
  completed with JSON output and no assertion/deadlock. End occupancy: 512
  translation blocks. The ROI had 51,559 successful lookups/probes and 49,729
  coordinated completions. These are functional smoke-test counts, not benchmark
  speedup claims. Both insertion paths are covered by the unit tests.
