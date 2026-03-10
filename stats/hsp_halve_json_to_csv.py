#!/usr/bin/env python3
"""Convert hsp_buffer_halve_history JSON into a single wide CSV.

Each snapshot (cache+cycle) occupies exactly 3 columns:
  <label>_entry_index, <label>_page_vpn, <label>_counter
"""

import argparse
import csv
import json
from pathlib import Path
from typing import Any, Dict, List, Tuple


def _load_json(json_path: Path) -> Dict[str, Any]:
    with json_path.open("r", encoding="utf-8") as f:
        obj = json.load(f)
    if not isinstance(obj, dict):
        raise ValueError("Input JSON root must be an object.")
    history = obj.get("hsp_buffer_halve_history")
    if not isinstance(history, dict):
        raise ValueError("Missing or invalid 'hsp_buffer_halve_history' in JSON root.")
    return history


def _sanitize_name(name: str) -> str:
    return "".join(ch if ch.isalnum() or ch in ("-", "_") else "_" for ch in name)


def _rows_from_snapshot(snapshot: Dict[str, Any]) -> Tuple[int, List[Tuple[int, int, int]]]:
    cycle = int(snapshot.get("cycle", 0) or 0)
    vpns = snapshot.get("entry_vpns", [])
    counters = snapshot.get("entry_counters", [])

    if not isinstance(vpns, list):
        vpns = []
    if not isinstance(counters, list):
        counters = []

    rows: List[Tuple[int, int, int]] = []
    for idx, counter in enumerate(counters):
        vpn = int(vpns[idx]) if idx < len(vpns) else 0
        rows.append((int(idx), vpn, int(counter or 0)))

    return cycle, rows


def _default_csv_path(json_path: Path) -> Path:
    return json_path.with_suffix("").with_name(f"{json_path.with_suffix('').name}_hsp_halve.csv")


def main() -> None:
    parser = argparse.ArgumentParser(description="Export HSP halve history into a single CSV with 3 columns per cycle.")
    parser.add_argument("--json", required=True, help="Path to input JSON.")
    parser.add_argument("--csv", help="Path to output CSV (default: <json>_hsp_halve.csv).")
    parser.add_argument("--cache", help="Only export one cache name (optional).")
    args = parser.parse_args()

    json_path = Path(args.json)
    history = _load_json(json_path)

    blocks: List[Tuple[str, List[Tuple[int, int, int]]]] = []
    for cache_name, snapshots_any in history.items():
        if args.cache and cache_name != args.cache:
            continue
        if not isinstance(snapshots_any, list):
            continue

        safe_cache = _sanitize_name(str(cache_name))
        for snapshot_any in snapshots_any:
            if not isinstance(snapshot_any, dict):
                continue
            cycle, rows = _rows_from_snapshot(snapshot_any)
            label = f"{safe_cache}_cycle_{cycle}"
            blocks.append((label, rows))

    out_file = Path(args.csv) if args.csv else _default_csv_path(json_path)

    with out_file.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)

        header: List[str] = []
        for label, _ in blocks:
            header.extend([f"{label}_entry_index", f"{label}_page_vpn", f"{label}_counter"])
        writer.writerow(header)

        max_rows = max((len(rows) for _, rows in blocks), default=0)
        for row_idx in range(max_rows):
            out_row: List[Any] = []
            for _, rows in blocks:
                if row_idx < len(rows):
                    entry_index, page_vpn, counter = rows[row_idx]
                    out_row.extend([entry_index, page_vpn, counter])
                else:
                    out_row.extend(["", "", ""])
            writer.writerow(out_row)

    print(f"Wrote {out_file} with {len(blocks)} snapshot block(s)")


if __name__ == "__main__":
    main()
