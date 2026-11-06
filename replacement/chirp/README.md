# CHiRP

Implements **CHiRP: Control-Flow History Reuse Prediction**, Mirbagher-Ajorpaz,
Garza, Pokam, and Jiménez, MICRO 2020, DOI
[10.1109/MICRO50266.2020.00023](https://doi.org/10.1109/MICRO50266.2020.00023).
The local reference is `/home/yuchenhe/2610.07685v1.pdf`.

## Use

From the repository root:

```sh
./config.sh chirp_config.json
make -j4
bin/champsim_chirp --warmup-instructions 1000000 --simulation-instructions 10000000 /path/to/trace.champsimtrace.xz
```

The example config selects CHiRP for the STLB and otherwise uses ChampSim's
built-in defaults. To use an existing machine configuration, set
`"replacement": "chirp"` inside its `STLB` object. The module is available to
other caches/TLBs too, but the paper designs and evaluates it for the L2 TLB.

## Algorithm

- Signature: `(PC >> 2) XOR path XOR conditional_history XOR indirect_history`,
  mixed to 16 bits with the 64-bit integer mix in reference [72]. Store that
  16-bit value in each entry; use it modulo the predictor table size for training
  and prediction (no second hashing of an already compressed signature).
- Path: one 64-bit register per CPU per policy, shifted left four bits and ORed
  with extracted `PC[3:2]` after each access (Figure 5, lines 28–30).
- Branch histories: two 64-bit registers per CPU, each shifted left eight bits
  and ORed with extracted `PC[11:4]`. Conditional branches update one register;
  indirect jumps, indirect calls, and returns update the other. Direct jumps
  and direct calls do not update either. Branch outcomes and targets are unused.
- Prefer the first dead way in way order; otherwise evict LRU. Invalid ways
  always take priority. On valid live eviction, increment the old signature's
  saturating counter. Dead evictions do not train.
- On insertion, predict dead, set `first_hit`, and make the entry MRU. On the
  first subsequent hit, decrement the old signature's saturating counter,
  refresh the prediction, and clear `first_hit`. Later hits only replace the
  stored signature and update LRU, as in Figure 5. Counter reads precede the
  training update when signatures alias.

The default predictor has **4096 logical 2-bit counters**, initially zero,
with **dead iff counter > 1**. The paper explores several table sizes and does
not specify initialization or a numerical dead threshold; these are explicit
implementation choices. Constants are in `chirp.h`. Counters occupy bytes in
this software model, rather than being packed into a hardware 1 KiB array.

## ChampSim integration

The CPU records branch PCs at its existing front-end branch prediction point.
`O3_CPU::initialize()` clears its branch histories. The replacement module uses
the available front-end branch history at TLB tag lookup; this is a timing
adaptation for ChampSim, whose requests do not carry the paper's detailed
instruction/branch trace context. It does not recreate a speculative history
and rollback model.

Signatures for misses are captured at tag lookup and retained until fill, so
intervening accesses and branch predictions cannot change the insertion
signature. Blocked retries and MSHR-merged accesses to the same translation on
the same CPU retain the first request's signature and count once in path
history. Fill removes merged pending signatures, including other CPUs sharing
that translation. Prediction/training occur at fill using the then-current
counter table. Direct fills without a preceding lookup compute a signature at
fill. Writeback hits are ignored; writeback fills insert live metadata without
training that writeback's signature on a later eviction. Invalidated entries
do not produce dead training.

Each policy instance has its own predictor, LRU state, and per-CPU path
histories; CPU branch histories are shared across policies on that CPU. The
paper's evaluation is single-core; per-CPU histories are an isolation extension,
not a claim to reproduce the paper's reported performance on multicore traces.

## Validation

```sh
make test selected_test='[chirp]'
```

Tests cover dead-first selection, LRU, invalid ways, counter saturation,
first-hit training, alias read ordering, delayed fills, retries, cross-CPU
merging, history rollover/isolation, CPU branch callbacks, and invalidation.
