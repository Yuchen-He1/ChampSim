#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
print_phases_plainstyle.py — Print ChampSim phases JSON in "cpuX->CACHE ..." plain_printer style.

Usage:
  python3 print_phases_plainstyle.py --json phases.json            # print both roi & sim
  python3 print_phases_plainstyle.py --json phases.json --which roi  # only roi
  python3 print_phases_plainstyle.py --json phases.json --which sim  # only sim
"""

import argparse, json, sys
from typing import Any, Dict, List, Tuple

ACCESS_TYPES = ["LOAD", "RFO", "PREFETCH", "WRITE", "TRANSLATION"]

def safe_list_num(v, idx):
    """JSON sometimes stores per-cpu counts as 1-element arrays; handle both scalar and list."""
    if isinstance(v, list):
        if idx < len(v): return v[idx]
        return 0
    return v if idx == 0 else 0

def get_prefetch_block(cache: Dict[str, Any]) -> Tuple[int,int,int,int]:
    return (
        int(cache.get("prefetch requested", 0) or 0),
        int(cache.get("prefetch issued", 0) or 0),
        int(cache.get("useful prefetch", 0) or 0),
        int(cache.get("useless prefetch", 0) or 0),
    )

def get_pin_block(cache: Dict[str, Any]) -> Tuple[int,int,int,int]:
    pin = cache.get("pin", {})
    return (
        int(pin.get("lines", 0) or 0),
        int(pin.get("hits", 0) or 0),
        int(pin.get("bypass_on_full", 0) or 0),
        int(pin.get("evicted_colder", 0) or 0),
    )

def collect_caches(section: Dict[str, Any]) -> List[Tuple[str, Dict[str, Any]]]:
    res = []
    for name, node in section.items():
        if name in ("DRAM", "cores"): 
            continue
        if isinstance(node, dict):
            res.append((name, node))
    return res

def num_cpus_from_cache(cache: Dict[str, Any]) -> int:
    # infer cpu count from first available type array length
    for t in ACCESS_TYPES:
        blk = cache.get(t, {})
        if isinstance(blk, dict):
            for key in ("hit", "miss", "mshr_merge"):
                v = blk.get(key)
                if isinstance(v, list):
                    return max(1, len(v))
    # fall back to 1
    return 1

def sum_over_types(cache: Dict[str, Any], cpu_idx: int) -> Tuple[int,int,int]:
    tot_hit = tot_miss = tot_merge = 0
    for t in ACCESS_TYPES:
        blk = cache.get(t, {})
        h = int(safe_list_num(blk.get("hit", 0), cpu_idx) or 0)
        m = int(safe_list_num(blk.get("miss", 0), cpu_idx) or 0)
        mg = int(safe_list_num(blk.get("mshr_merge", 0), cpu_idx) or 0)
        tot_hit += h; tot_miss += m; tot_merge += mg
    return tot_hit, tot_miss, tot_merge

def print_cache_block(cache_name: str, cache: Dict[str, Any], cpu_idx: int):
    tot_hit, tot_miss, tot_merge = sum_over_types(cache, cpu_idx)
    # TOTAL line
    print(f"cpu{cpu_idx}->{cache_name} {'TOTAL':<12s} ACCESS: {tot_hit + tot_miss:10d} HIT: {tot_hit:10d} MISS: {tot_miss:10d} MSHR_MERGE: {tot_merge:10d}")
    # Per-type lines
    for t in ACCESS_TYPES:
        blk = cache.get(t, {})
        h = int(safe_list_num(blk.get('hit', 0), cpu_idx) or 0)
        m = int(safe_list_num(blk.get('miss', 0), cpu_idx) or 0)
        mg = int(safe_list_num(blk.get('mshr_merge', 0), cpu_idx) or 0)
        print(f"cpu{cpu_idx}->{cache_name} {t:<12s} ACCESS: {h+m:10d} HIT: {h:10d} MISS: {m:10d} MSHR_MERGE: {mg:10d}")
    # Prefetch block (cache-level)
    pref_req, pref_iss, pref_use, pref_usl = get_prefetch_block(cache)
    print(f"cpu{cpu_idx}->{cache_name} PREFETCH REQUESTED: {pref_req:10d} ISSUED: {pref_iss:10d} USEFUL: {pref_use:10d} USELESS: {pref_usl:10d}")
    # Average miss latency (cache-level)
    miss_lat = cache.get("miss latency", 0.0) or 0.0
    try:
        miss_lat = float(miss_lat)
    except Exception:
        miss_lat = 0.0
    print(f"cpu{cpu_idx}->{cache_name} AVERAGE MISS LATENCY: {miss_lat} cycles")
    # Pin block (optional)
    pl, ph, pb, pe = get_pin_block(cache)
    if any([pl, ph, pb, pe]):
        print(f"{cache_name} PIN LINES: {pl:10d} HITS: {ph:10d} BYPASS_ON_FULL: {pb:10d} EVICTED_COLDER: {pe:10d}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", required=True, help="Phases JSON file")
    ap.add_argument("--which", choices=["roi","sim","both"], default="both", help="Which section(s) to print")
    args = ap.parse_args()

    with open(args.json, "r") as f:
        root = json.load(f)
    end_cycle = root.get("end_cycle")

    phases = root.get("phases", [])
    if not isinstance(phases, list) or not phases:
        print("No phases found.", file=sys.stderr)
        sys.exit(1)
    if end_cycle is not None:
        print(f"=== End Cycle: {end_cycle} ===")

    for ph in phases:
        pname = ph.get("name", "phase")
        print(f"=== {pname} ===")
        for sec_name in (["roi","sim"] if args.which=="both" else [args.which]):
            sec = ph.get(sec_name, {})
            print(("Region of Interest Statistics" if sec_name=="roi" else "Total Simulation Statistics (not including warmup)"))
            caches = collect_caches(sec)
            for cache_name, cache in caches:
                ncpu = num_cpus_from_cache(cache)
                # For caches with only one CPU value encoded as list of size 1, we still format as cpu0->
                for cpu_idx in range(ncpu):
                    print_cache_block(cache_name, cache, cpu_idx)
                print()  # blank line between caches

            if sec_name == "roi":
                # DRAM Statistics section printing (optional)
                dram = sec.get("DRAM", [])
                if isinstance(dram, list) and dram:
                    print("DRAM Statistics")
                    for i, ch in enumerate(dram):
                        name = ch.get("name", f"DRAM{i}")
                        rb_hit = ch.get("RQ ROW_BUFFER_HIT", 0)
                        rb_miss = ch.get("RQ ROW_BUFFER_MISS", 0)
                        avg_db = ch.get("AVG DBUS CONGESTED CYCLE", 0)
                        wq_hit = ch.get("WQ ROW_BUFFER_HIT", 0)
                        wq_miss = ch.get("WQ ROW_BUFFER_MISS", 0)
                        refresh = ch.get("REFRESHES ISSUED", 0)
                        print(f"{name} RQ ROW_BUFFER_HIT: {rb_hit:10}")
                        print(f"  ROW_BUFFER_MISS: {rb_miss:10}")
                        print(f"  AVG DBUS CONGESTED CYCLE: {avg_db}")
                        print(f"{name} WQ ROW_BUFFER_HIT: {wq_hit:10}")
                        print(f"  ROW_BUFFER_MISS: {wq_miss:10}")
                        print(f"{name} REFRESHES ISSUED: {refresh if refresh else '-'}")
                    print()

if __name__ == "__main__":
    main()
