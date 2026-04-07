#!/usr/bin/env python3
"""Sum numeric columns in a CSV file, tolerant to mixed/irregular rows."""

import argparse
import csv
from pathlib import Path
from typing import Dict, List


def parse_number(raw: str) -> float | None:
    s = raw.strip()
    if not s:
        return None
    try:
        return float(s)
    except ValueError:
        return None


def detect_header(rows: List[List[str]]) -> int:
    """Pick the first widest row as header."""
    max_len = max((len(r) for r in rows), default=0)
    for idx, r in enumerate(rows):
        if len(r) == max_len and max_len > 0:
            return idx
    return -1


def main() -> None:
    parser = argparse.ArgumentParser(description="Sum numeric columns in a CSV (robust to mixed row formats).")
    parser.add_argument("csv_file", help="Path to input CSV.")
    parser.add_argument("--out", help="Optional output CSV path for one-row sums.")
    args = parser.parse_args()

    csv_path = Path(args.csv_file)
    with csv_path.open("r", encoding="utf-8", newline="") as f:
        rows = list(csv.reader(f))

    if not rows:
        raise ValueError(f"Empty CSV: {csv_path}")

    header_idx = detect_header(rows)
    if header_idx < 0:
        raise ValueError(f"Cannot detect a valid header row in: {csv_path}")

    header = rows[header_idx]
    sums: Dict[str, float] = {col: 0.0 for col in header}
    seen_numeric: Dict[str, bool] = {col: False for col in header}

    for row in rows[header_idx + 1 :]:
        if len(row) != len(header):
            continue
        for i, col in enumerate(header):
            num = parse_number(row[i])
            if num is not None:
                sums[col] += num
                seen_numeric[col] = True

    numeric_cols = [c for c in header if seen_numeric[c]]

    print(f"[sum] {csv_path}")
    for col in numeric_cols:
        print(f"{col},{sums[col]}")

    if args.out:
        out_path = Path(args.out)
        with out_path.open("w", encoding="utf-8", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(numeric_cols)
            writer.writerow([sums[c] for c in numeric_cols])
        print(f"[written] {out_path}")


if __name__ == "__main__":
    main()
