#!/usr/bin/env python3

import argparse
import csv
from typing import Dict, List

# Columns expected in the input CSV (see stats/csv/*.csv)
REQUIRED_COLUMNS = [
    "vpn",
    "dtlb_hit",
    "stlb_hit",
    "hsp_hit",
    "stlb_ptw",
]

# Downstream translation demand used to rank hot pages
ACCESS_NUM_COLUMNS = ["dtlb_hit", "stlb_hit", "hsp_hit", "stlb_ptw"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Report PTW totals for the top-N hot pages found in a TLB CSV file. "
            "Hot pages must satisfy the downstream translation demand threshold "
            "(dtlb_hit + stlb_hit + stlb_miss) — referred to as the access number — "
            "and are ranked by their PTW count derived from the selected last-level TLB."
        )
    )
    # Example:
    #   python stats/hot_page_ptw_summary.py stats/g22sssp_vc_10m.csv 5000 0 --sort-by access
    parser.add_argument(
        "csv_file",
        help=(
            "CSV path with columns: vpn,dtlb_hit,stlb_hit,hsp_hit,stlb_ptw."
        ),
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
        "--sort-by",
        choices=("ptw", "access"),
        default="ptw",
        help="Sort hot pages by 'ptw' or by access_num. Default: ptw.",
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
            row["ptw"] = row["stlb_ptw"]
            hot_candidates.append(row)

    if not hot_candidates:
        print("No pages met the hot-page threshold.")
        return

    if args.sort_by == "access":
        hot_candidates.sort(key=lambda r: r["access_num"], reverse=True)
    else:
        hot_candidates.sort(key=lambda r: r["ptw"], reverse=True)
    topn = max(args.topn, 0)
    if topn == 0:
        print("topn was 0; no hot pages to report.")
        return

    top_pages = hot_candidates[:topn]
    total_ptw = sum(row["ptw"] for row in top_pages)

    level_label = "STLB PTW"
    print(
        f"Top {len(top_pages)} hot pages (threshold={args.threshold}, PTW source={level_label}, "
        f"sort_by={args.sort_by}):"
    )
    print(f"Total PTW count: {total_ptw}")
    for idx, row in enumerate(top_pages, start=1):
        vpn = row["vpn"]
        ptw = row["ptw"]
        access_num = row["access_num"]
        print(f"{idx}. vpn={vpn} ptw={ptw} access_num={access_num}")
    #print(f"Sum of PTW across top {len(top_pages)} pages: {total_ptw}")


if __name__ == "__main__":
    main()
