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
It recognizes 30 trace files: BFS, BC, CC, PR and SSSP each have Kronecker,
LiveJournal, Orkut, Wiki, road and U22. g23 and TC files are ignored. No default
baselines are launched. All workloads use 50M warmup, with ROI capped at 1B.
Historical baseline ROI lengths below 1B are retained in ``SHORT_ROIS``.
These are compared instruction ranges, not a claim that the file has that many
records after subtracting warmup.

``--jobs all`` (the default) selects every supported input that exists. Missing
files are skipped. ``--jobs bfs bc cc`` selects algorithm groups, and
individual job names can be mixed with groups. ``--parallel`` is an upper limit:
with 30 available jobs and ``--parallel 30``, all 30 can start together.
The script does not infer new algorithms from arbitrary filenames.

BF environment configuration
----------------------------

The package includes ``bf_env.sh`` (tracked as ``scripts/hsp_bf_env.sh``).
Edit and source it with ``. ./bf_env.sh`` before invoking ``run.py``. It exports
``CHAMPSIM_STLB_HSP_BF_HASHES``, ``CHAMPSIM_STLB_HSP_BF_COUNTERS``,
``CHAMPSIM_STLB_HSP_BF_FALSE_POSITIVE_RATE`` and
``CHAMPSIM_STLB_HSP_BF_LATENCY``. Sourcing overwrites these four variables;
one-off overrides may be exported after sourcing. Running it with ``bash``
instead of sourcing does not modify the caller's environment.

The runtime script preserves these four BF overrides, validates them, and records
them in both dry-run output and each result manifest/log. Other HSP settings
remain fixed for the selected experiment variant. Counter width still requires
choosing the appropriate compiled binary. No recompile is needed to change
hash count, counter count or BF latency. The target FP setting does not force
the measured FP rate when counter and hash counts are explicitly specified.
