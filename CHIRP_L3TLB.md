# 4K-entry L3 TLB with CHiRP

Build the configuration used for the UM L3 CHiRP binary:

```sh
./config.sh L3tlb_config_4kL3_chirp.json
make -j4
```

The executable is `bin/champsim-l3tlb-4k-chirp`.

- ITLB/DTLB -> STLB -> L3TLB -> PTW, with 4 KiB pages.
- STLB: 128 sets x 12 ways = 1536 entries, 4 cycles, LRU.
- L3TLB: 256 sets x 16 ways = 4096 entries, 12 cycles, CHiRP.
- The other machine parameters match the existing 4K L3TLB baseline.

This branch combines hierarchy support from commit
`a5e7b09a45e0525a5c06fe5f2a38d22d71c96d0c` with the CHiRP module,
CPU branch-history hook, and tests from
`eb6331af282503949a52a2b4d91af3b5a022b7b7`.
The CHiRP prediction algorithm is unchanged from that commit. Its implementation
and timing-adaptation notes are in `replacement/chirp/README.md`; the STLB
example in that document is general module documentation. Use the L3 config
above for this branch's experiment.

The source/build files and complete configuration were checked byte-for-byte
against those used for the already distributed static UM binary:

- Binary SHA-256: `5990930e33d8e47fe6410cb78f7936c83afbf776f59074280f51c5e8d46e93bc`.
- Exported source-tree SHA-256: `80fd803fa1034a9defc168a20921826b26d0457b34a650b6782ffe2497b531fd`.
- Canonical configuration SHA-256: `4db9798f5608613cf9a678f8684cb13ab58dda12c8815132bc72cca1e126f58e`.

Binary hashes need not match when rebuilt with a different compiler/linker or
absolute build path. The static-link flag is a build choice, not a simulator
policy or configuration change. Binaries, experiment outputs, and transfer
packages are not committed here.
