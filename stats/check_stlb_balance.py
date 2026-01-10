#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Check that stlb_acc == stlb_hit + hsp_hit + (stlb_acc - stlb_hit - hsp_hit) per page.

Usage:
  python stats/check_stlb_balance.py --json /path/to/stats.json
  python stats/check_stlb_balance.py --csv /path/to/tlb_summary.csv
"""

import argparse
import csv
import json
from typing import Dict, Iterable, Tuple


def _load_json_rows(path: str) -> Iterable[Tuple[Dict[str, int], Dict[str, int]]]:
    with open(path, "r", encoding="utf-8") as f:
        obj = json.load(f)
    records = obj.get("per_page_translation", [])
    for row in records:
        raw = row.get("raw", {})
        yield row, raw


def _load_csv_rows(path: str) -> Iterable[Dict[str, int]]:
    with open(path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            yield row


def _as_int(value) -> int:
    try:
        return int(value)
    except Exception:
        return 0


def check_json(path: str) -> int:
    mismatches = 0
    total = 0
    for row, raw in _load_json_rows(path):
        total += 1
        stlb_acc = _as_int(raw.get("stlb_acc"))
        stlb_hit = _as_int(raw.get("stlb_hit"))
        hsp_hit = _as_int(raw.get("hsp_hit"))
        stlb_ptw = _as_int(raw.get("stlb_ptw"))
        computed_ptw = stlb_acc - stlb_hit - hsp_hit
        if stlb_acc != (stlb_hit + hsp_hit + computed_ptw):
            mismatches += 1
            print(
                f"Mismatch: core={row.get('core')} vpn={row.get('vpn')} "
                f"stlb_acc={stlb_acc} stlb_hit={stlb_hit} hsp_hit={hsp_hit} "
                f"stlb_ptw={stlb_ptw} computed_ptw={computed_ptw}"
            )
    print(f"Checked {total} rows, mismatches: {mismatches}")
    return mismatches


def check_csv(path: str) -> int:
    mismatches = 0
    total = 0
    for row in _load_csv_rows(path):
        total += 1
        stlb_acc = _as_int(row.get("stlb_acc"))
        stlb_hit = _as_int(row.get("stlb_hit"))
        hsp_hit = _as_int(row.get("hsp_hit"))
        stlb_ptw = _as_int(row.get("stlb_ptw"))
        computed_ptw = stlb_acc - stlb_hit - hsp_hit
        if stlb_acc != (stlb_hit + hsp_hit + computed_ptw):
            mismatches += 1
            print(
                f"Mismatch: {row.get('vpn') or row.get('core') or row.get('is_instr')} "
                f"stlb_acc={stlb_acc} stlb_hit={stlb_hit} hsp_hit={hsp_hit} "
                f"stlb_ptw={stlb_ptw} computed_ptw={computed_ptw}"
            )
    print(f"Checked {total} rows, mismatches: {mismatches}")
    return mismatches


def main() -> None:
    parser = argparse.ArgumentParser(description="Check stlb_acc balance against stlb_hit + hsp_hit + computed PTW.")
    parser.add_argument("--json", help="Path to JSON with per_page_translation.")
    parser.add_argument("--csv", help="Path to CSV with stlb_acc/stlb_hit/hsp_hit/stlb_ptw.")
    args = parser.parse_args()

    if bool(args.json) == bool(args.csv):
        raise SystemExit("Provide exactly one of --json or --csv.")

    if args.json:
        check_json(args.json)
    else:
        check_csv(args.csv)


if __name__ == "__main__":
    main()
