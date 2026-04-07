#!/usr/bin/env python3
"""Compute approximate working-set and simple access stats from HSP wide CSV.

Input CSV format (from hsp_halve_json_to_csv.py):
  <cache>_cycle_<cycle>_entry_index,
  <cache>_cycle_<cycle>_page_vpn,
  <cache>_cycle_<cycle>_counter,
  ...

Simple access inference between consecutive halve snapshots (by VPN):
- baseline_no_access = floor(prev_counter / 2)
- accessed_flag = (curr_counter > baseline_no_access)

Output:
  hsp_approx_working_set.csv with per-cycle working-set metrics plus:
  - new_vpns
  - pages_accessed_flag
"""
# python stats/analyze_hsp_hotness.py \
#   --csv <input_wide_csv> \
#   --cache cpu0_STLB \
#   --skip-first-halves 10 \
#   --thresholds 1,2,4,8

import argparse
import csv
import re
import statistics
from pathlib import Path
from typing import Dict, List, Tuple

import pandas as pd

ENTRY_SUFFIX = "_entry_index"
VPN_SUFFIX = "_page_vpn"
COUNTER_SUFFIX = "_counter"
CYCLE_PATTERN = re.compile(r"^(?P<cache>.+)_cycle_(?P<cycle>\d+)_entry_index$")


def parse_header(header: List[str], cache_filter: str | None) -> List[Tuple[str, int, int, int]]:
    """Return blocks as (cache_name, cycle, vpn_col_idx, counter_col_idx), sorted by cycle."""
    name_to_idx: Dict[str, int] = {name: idx for idx, name in enumerate(header)}
    blocks: List[Tuple[str, int, int, int]] = []

    for name in header:
        m = CYCLE_PATTERN.match(name)
        if not m:
            continue

        cache_name = m.group("cache")
        if cache_filter and cache_name != cache_filter:
            continue

        cycle = int(m.group("cycle"))
        base = name[:-len(ENTRY_SUFFIX)]
        vpn_name = f"{base}{VPN_SUFFIX}"
        counter_name = f"{base}{COUNTER_SUFFIX}"
        if vpn_name not in name_to_idx or counter_name not in name_to_idx:
            continue

        blocks.append((cache_name, cycle, name_to_idx[vpn_name], name_to_idx[counter_name]))

    blocks.sort(key=lambda x: x[1])
    return blocks


def parse_thresholds(raw: str) -> List[int]:
    vals: List[int] = []
    for x in raw.split(","):
        x = x.strip()
        if not x:
            continue
        v = int(x)
        if v < 0:
            raise ValueError("Thresholds must be >= 0")
        vals.append(v)
    if not vals:
        raise ValueError("No valid thresholds provided")
    return sorted(set(vals))


def load_snapshots(csv_path: Path, blocks: List[Tuple[str, int, int, int]]) -> Dict[int, Dict[int, int]]:
    """Load snapshots as per-cycle map: cycle -> {vpn: max_counter_in_snapshot}.

    If the same VPN appears multiple times in a snapshot, keep max counter.
    """
    snapshots: Dict[int, Dict[int, int]] = {cycle: {} for _, cycle, _, _ in blocks}

    with csv_path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.reader(f)
        _ = next(reader, None)

        for row in reader:
            for _, cycle, vpn_col, counter_col in blocks:
                if vpn_col >= len(row) or counter_col >= len(row):
                    continue

                vpn_raw = row[vpn_col].strip()
                ctr_raw = row[counter_col].strip()
                if not vpn_raw or not ctr_raw:
                    continue

                try:
                    vpn = int(vpn_raw)
                    ctr = int(ctr_raw)
                except ValueError:
                    continue

                prev = snapshots[cycle].get(vpn)
                if prev is None or ctr > prev:
                    snapshots[cycle][vpn] = ctr

    return snapshots


def ws_counts(vpn_to_ctr: Dict[int, int], thresholds: List[int]) -> Dict[int, int]:
    vals = list(vpn_to_ctr.values())
    return {t: sum(v >= t for v in vals) for t in thresholds}


def infer_access(prev_map: Dict[int, int], curr_map: Dict[int, int]) -> Dict[str, int]:
    prev_vpns = set(prev_map.keys())
    curr_vpns = set(curr_map.keys())
    common = prev_vpns & curr_vpns

    new_vpns = curr_vpns - prev_vpns

    pages_accessed = 0

    for vpn in common:
        p = prev_map[vpn]
        c = curr_map[vpn]

        baseline = p // 2
        if c > baseline:
            pages_accessed += 1

    result = {
        "common_vpns": len(common),
        "new_vpns": len(new_vpns),
        "pages_accessed_flag": pages_accessed,
    }
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description="Compute approximate WS and simple access stats from HSP wide CSV.")
    parser.add_argument("--csv", required=True, help="Input wide CSV path.")
    parser.add_argument("--cache", help="Cache filter, e.g., cpu0_STLB.")
    parser.add_argument("--skip-first-halves", type=int, default=0, help="Skip first N halve snapshots (useful to ignore warmup carry-over).")
    parser.add_argument("--stride", type=int, default=1, help="Use one every N cycles after sorting (default: 1).")
    parser.add_argument("--max-cycles", type=int, default=0, help="Use at most N cycles (0 means all).")
    parser.add_argument("--thresholds", default="1,2,4,8", help="Comma-separated thresholds used for both WS and access stats.")
    args = parser.parse_args()

    if args.skip_first_halves < 0:
        raise ValueError("--skip-first-halves must be >= 0")
    if args.stride <= 0:
        raise ValueError("--stride must be >= 1")

    thresholds = parse_thresholds(args.thresholds)

    csv_path = Path(args.csv)

    with csv_path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.reader(f)
        header = next(reader, None)
    if not header:
        raise ValueError("CSV is empty or missing header")

    blocks = parse_header(header, args.cache)
    if not blocks:
        raise ValueError("No cycle blocks found. Check CSV format and --cache.")

    if args.skip_first_halves:
        blocks = blocks[args.skip_first_halves :]

    blocks = blocks[:: args.stride]
    if args.max_cycles and args.max_cycles > 0:
        blocks = blocks[: args.max_cycles]

    if not blocks:
        raise ValueError("No cycles left after filtering (--skip-first-halves/--stride/--max-cycles).")

    snapshots = load_snapshots(csv_path, blocks)

    rows: List[Dict[str, object]] = []
    prev_cycle = None
    prev_map: Dict[int, int] = {}

    for _, cycle, _, _ in blocks:
        curr_map = snapshots[cycle]
        counter_values = list(curr_map.values())
        if counter_values:
            max_counter = max(counter_values)
            mean_counter = statistics.mean(counter_values)
            median_counter = statistics.median(counter_values)
            std_counter = statistics.pstdev(counter_values)
        else:
            max_counter = 0
            mean_counter = 0.0
            median_counter = 0.0
            std_counter = 0.0

        row: Dict[str, object] = {
            "cycle": cycle,
            "unique_vpns": len(curr_map),
            "max_counter": max_counter,
            "mean_counter": mean_counter,
            "median_counter": median_counter,
            "std_counter": std_counter,
        }

        ws = ws_counts(curr_map, thresholds)
        for t in thresholds:
            row[f"approx_ws_ge_{t}"] = ws[t]

        if prev_cycle is None:
            row["common_vpns"] = 0
            row["new_vpns"] = len(curr_map)
            row["pages_accessed_flag"] = 0
        else:
            infer = infer_access(prev_map, curr_map)
            row.update(infer)

        rows.append(row)
        prev_cycle = cycle
        prev_map = curr_map

    out_csv = csv_path.with_name(f"{csv_path.stem}_filter_hsp_approx_working_set.csv")
    pd.DataFrame(rows).to_csv(out_csv, index=False)

    print(f"Wrote: {out_csv}")
    print(f"Cycles analyzed: {len(rows)}")
    print(f"Thresholds: {thresholds}")


if __name__ == "__main__":
    main()
