#!/usr/bin/env python3

import argparse
import csv
from typing import Dict, List

# Columns expected in the input CSV (see stats/csv/*.csv)
REQUIRED_COLUMNS = [
    "vpn",
    "itlb_hit",
    "dtlb_hit",
    "stlb_hit",
    "stlb_miss",
    "l3tlb_hit",
    "l3tlb_miss",
]

# Downstream translation demand (subset) used to rank hot pages
ACCESS_NUM_COLUMNS = ["dtlb_hit", "stlb_hit", "stlb_miss"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Report PTW totals for the top-N hot pages found in a TLB CSV file. "
            "Hot pages must satisfy the downstream translation demand threshold "
            "(dtlb_hit + stlb_hit + stlb_miss) — referred to as the access number — "
            "and are ranked by their PTW count derived from the selected last-level TLB."
        )
    )
    parser.add_argument(
        "csv_file",
        help="Path to a CSV file with columns: vpn,itlb_hit,dtlb_hit,stlb_hit,stlb_miss,l3tlb_hit,l3tlb_miss",
    )
    parser.add_argument(
        "topn",
        type=int,
        help="Number of hot pages to include in the report.",
    )
    parser.add_argument(
        "threshold",
        type=int,
        help="Minimum downstream translation demand required for a page to be considered hot.",
    )
    parser.add_argument(
        "--last-level",
        choices=("l3", "stlb"),
        default="l3",
        help="Which TLB level supplies the PTW counts: 'l3' (use l3tlb_miss) or 'stlb' (use stlb_miss). Default: l3.",
    )
    return parser.parse_args()


def read_rows(path: str) -> List[Dict[str, int]]:
    with open(path, "r", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        if reader.fieldnames is None:
            raise ValueError(f"No header row found in {path!r}.")

        missing = [col for col in REQUIRED_COLUMNS if col not in reader.fieldnames]
        if missing:
            missing_str = ", ".join(missing)
            raise ValueError(f"Missing expected columns in {path!r}: {missing_str}")

        rows: List[Dict[str, int]] = []
        for raw_row in reader:
            row: Dict[str, int] = {}
            for col in REQUIRED_COLUMNS:
                try:
                    row[col] = int(raw_row[col])
                except (TypeError, ValueError) as exc:
                    raise ValueError(f"Non-integer value for column '{col}' in row: {raw_row}") from exc
            rows.append(row)
        return rows


def main() -> None:
    args = parse_args()
    rows = read_rows(args.csv_file)

    hot_candidates: List[Dict[str, int]] = []
    for row in rows:
        access_num = sum(row[col] for col in ACCESS_NUM_COLUMNS)
        if access_num >= args.threshold:
            row = dict(row)  # copy so we do not mutate the original list
            row["access_num"] = access_num
            if args.last_level == "stlb":
                row["ptw"] = row["stlb_miss"]
            else:
                row["ptw"] = row["l3tlb_miss"]
            hot_candidates.append(row)

    if not hot_candidates:
        print("No pages met the hot-page threshold.")
        return

    hot_candidates.sort(key=lambda r: (r["access_num"], r["ptw"]), reverse=True)
    topn = max(args.topn, 0)
    if topn == 0:
        print("topn was 0; no hot pages to report.")
        return

    top_pages = hot_candidates[:topn]
    total_ptw = sum(row["ptw"] for row in top_pages)

    level_label = "STLB miss" if args.last_level == "stlb" else "L3TLB miss"
    print(f"Top {len(top_pages)} hot pages (threshold={args.threshold}, PTW source={level_label}):")
    print(f"Total PTW count: {total_ptw}")
    for idx, row in enumerate(top_pages, start=1):
        vpn = row["vpn"]
        ptw = row["ptw"]
        access_num = row["access_num"]
        print(f"{idx}. vpn={vpn} ptw={ptw} access_num={access_num}")
    #print(f"Sum of PTW across top {len(top_pages)} pages: {total_ptw}")


if __name__ == "__main__":
    main()
