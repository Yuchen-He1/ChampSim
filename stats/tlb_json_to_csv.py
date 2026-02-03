#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Convert per_page_translation JSON into a CSV (no plotting).

Usage:
  python stats/tlb_json_to_csv.py --json /path/to/your.json
  python stats/tlb_json_to_csv.py --json /path/to/your.json --csv /path/to/out.csv
"""

import argparse
import json
from typing import Any, Dict, List

import pandas as pd


def _load_json(json_path: str) -> Dict[str, Any]:
    with open(json_path, "r", encoding="utf-8") as f:
        text = f.read().strip()
        try:
            obj = json.loads(text)
            if isinstance(obj, dict) and "per_page_translation" in obj:
                return obj
        except Exception:
            pass
    records = []
    with open(json_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except Exception:
                continue
            if isinstance(obj, dict) and "per_page_translation" in obj:
                arr = obj["per_page_translation"]
                if isinstance(arr, list):
                    records.extend(arr)
    if not records:
        raise ValueError("Could not find 'per_page_translation' array in the provided JSON.")
    return {"per_page_translation": records}


def _default_csv_path(json_path: str) -> str:
    return f"{json_path.rsplit('.', 1)[0]}.csv"


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert per_page_translation JSON into a CSV.")
    parser.add_argument("--json", required=True, help="Path to input JSON.")
    parser.add_argument("--csv", help="Path to output CSV (default: JSON basename + .csv).")
    args = parser.parse_args()

    obj = _load_json(args.json)
    records = obj.get("per_page_translation", [])
    df = pd.json_normalize(records)
    end_cycle = obj.get("end_cycle")
    roi_cycles_hsp = obj.get("roi_cycles_with_hsp")

    # Keep vpn as string for readability/consistency
    if "vpn" in df.columns:
        df["vpn"] = df["vpn"].astype(str)

    if "is_instr" in df.columns:
        df = df[~df["is_instr"]]

    raw_stlb_acc = df["raw.stlb_acc"] if "raw.stlb_acc" in df.columns else 0
    raw_stlb_hit = df["raw.stlb_hit"] if "raw.stlb_hit" in df.columns else 0
    raw_hsp_hit = df["raw.hsp_hit"] if "raw.hsp_hit" in df.columns else 0

    df["dtlb_hit"] = df["raw.dtlb_hit"] if "raw.dtlb_hit" in df.columns else 0
    df["stlb_hit"] = raw_stlb_hit
    df["hsp_hit"] = raw_hsp_hit
    df["stlb_ptw"] = (raw_stlb_acc - raw_stlb_hit - raw_hsp_hit)
    df["hsp_hit_interval_sum"] = df["raw.hsp_hit_interval_sum"] if "raw.hsp_hit_interval_sum" in df.columns else 0
    df["hsp_hit_interval_count"] = df["raw.hsp_hit_interval_count"] if "raw.hsp_hit_interval_count" in df.columns else 0

    group_by = "vpn"
    agg = (
        df.groupby(group_by, dropna=False)[
            [
                "dtlb_hit",
                "stlb_hit",
                "hsp_hit",
                "stlb_ptw",
                "hsp_hit_interval_sum",
                "hsp_hit_interval_count",
            ]
        ]
        .sum()
        .reset_index()
    )
    agg["hsp_hit_interval_avg"] = agg["hsp_hit_interval_sum"] / agg["hsp_hit_interval_count"].replace(0, pd.NA)

    if end_cycle is not None:
        agg["end_cycle"] = end_cycle
    if roi_cycles_hsp is not None:
        agg["roi_cycles_with_hsp"] = roi_cycles_hsp

    csv_out = args.csv or _default_csv_path(args.json)
    agg.to_csv(csv_out, index=False)
    print(f"Wrote {csv_out}")


if __name__ == "__main__":
    main()
