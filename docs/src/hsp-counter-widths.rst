HSP counter widths and internal latency
======================================

The compiler definitions ``CHAMPSIM_HSP_COUNTER_BITS`` and
``CHAMPSIM_BF_COUNTER_BITS`` control effective saturating counter ranges.
Defaults are 10 and 8 bits. HSP hotness retains its original saturation at
512; a compiled 4-bit HSP binary clamps this to 15 even if the environment
requests 512 or unbounded counting. BF counters saturate at 255 or 15.
Integer containers are not packed: this models hardware counter behavior,
not host process memory usage.

The three experiment binaries use HSP/BF widths 4/8, 10/4 and 4/4. Only
``src/cache.cc`` uses these compiler definitions, so other objects can be shared
between links. Recompile that object when switching widths. JSON records
``bf_counter_bits``, ``hsp_hotness_counter_bits`` and
``hsp_hotness_saturation``. Startup text prints both widths and limits.

Default HSP organization is eight buffers of 512 entries, with seven hashes and
4912 counters per BF. The original default configuration uses an LRU STLB with
128 sets, 12 ways and 16 MSHRs, without an added L3 TLB.

Internal latency is enabled by default. STLB misses enqueue a BF stage (one
cycle); positive BFs add a nine-cycle HSP search for 512 entries. Buffers and
filters operate in parallel. Pending HSP lookups and PTW MSHRs share finite
credits; HSP hit returns share fill bandwidth with normal fills. Same-address
requests merge. Sampled results survive eviction during search, but promotion
requires the sampled entry to remain in its slot. Warmup skips added stage
delays. Set ``CHAMPSIM_STLB_HSP_INTERNAL_LATENCY=0`` for the legacy path.

Internal-mode ROI cycles already include these delays: JSON sets
``hsp_latency_model=internal`` and ``hsp_lookup_extra_cycles=0``. BF diagnostic
statistics apply to both paths; internal false negatives use membership sampled
at the BF boundary. The diagnostics never repair a missed HSP hit.

The runtime script ``scripts/run_hsp_counter_variants.py`` is distributed as
``run.py`` next to ``bin/champsim_hsp4_bf8``, ``bin/champsim_hsp10_bf4`` and
``bin/champsim_hsp4_bf4``. It requires ``--variant``, accepts separate PR and SSSP
trace directories, skips missing traces, and validates counter-width metadata.
It runs four SSSP inputs and five PR inputs; it does not launch default baselines.
All workloads use 50M warmup and a 1B ROI cap except SSSP Wiki (924,096,728).
