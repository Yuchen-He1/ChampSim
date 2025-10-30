#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TLB JSON (per_page_translation) → CSV & Overlay Plot (smallest on top per group)

Updates:
- Bars are visually continuous (width=1.0, no side margins, fixed xlim)
- Adds --max_xtick_labels to hide extra labels while keeping all bars
- Keeps per-group zorder so the smallest bar is drawn on top

Usage:
  python3 tlb_plot_hotpage.py \
    --json /path/to/your.json \
    --csv /path/to/tlb_summary.csv \
    --out_png /path/to/tlb_overlay.png \
    --group_by vpn \
    --alpha 0.55 \
    --logy \
    --topk 40 \
    --max_xtick_labels 20
"""

import json
import argparse
from typing import Optional, List, Dict, Any
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

METRICS = ["itlb_hit", "dtlb_hit", "stlb_hit","l3tlb_hit", "l3tlb_ptw"]  # from raw.*


def _load_per_page_records(json_path: str) -> List[Dict[str, Any]]:
    with open(json_path, "r", encoding="utf-8") as f:
        text = f.read().strip()
        # Full JSON object first
        try:
            obj = json.loads(text)
            if isinstance(obj, dict) and "per_page_translation" in obj:
                arr = obj["per_page_translation"]
                if not isinstance(arr, list):
                    raise ValueError("'per_page_translation' must be a list")
                return arr
        except Exception:
            pass
    # JSON lines fallback: collect arrays
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
    return records


def process_tlb_json_overlay(
    json_path: str,
    csv_out: str = "tlb_summary.csv",
    out_png: str = "tlb_overlay.png",
    alpha: float = 0.6,
    logy: bool = True,
    topk: Optional[int] = 50,
    group_by: str = "vpn",
    max_xtick_labels: int = 20,
) -> Dict[str, Any]:
    # Load & normalize
    records = _load_per_page_records(json_path)
    if len(records) == 0:
        raise ValueError("No records found under 'per_page_translation'.")
    df = pd.json_normalize(records)

    # Keep vpn as string for readability/consistency
    if group_by in df.columns and group_by == "vpn":
        df["vpn"] = df["vpn"].astype(str)

    # Promote raw.* counts to top-level metric names
    for m in METRICS:
        raw_col = f"raw.{m}"
        if raw_col in df.columns:
            df[m] = df[raw_col]
        else:
            df[m] = 0

    # Ensure group_by column exists
    if group_by not in df.columns:
        df[group_by] = "unknown"

    # Aggregate by group
    agg = df.groupby(group_by, dropna=False)[METRICS].sum().reset_index()

    # Save full CSV (all rows)
    csv_cols = [group_by] + METRICS
    agg.to_csv(csv_out, index=False)

    # Sort by total for plotting
    # agg["_total_for_sort"] = agg[METRICS].sum(axis=1)
    # agg = agg.sort_values("_total_for_sort", ascending=False)
    # Sort by downstream translation demand (DTLB hits + STLB/L3TLB misses)
    agg["_sort_key"] = agg["dtlb_hit"] + agg["stlb_hit"] + agg["stlb_ptw"] + agg["l3tlb_hit"] + agg["l3tlb_ptw"]
    agg = agg.sort_values("_sort_key", ascending=False)

    # Select top-K for plotting
    if topk is not None and topk > 0:
        plot_df = agg.head(topk).copy()
    else:
        plot_df = agg.copy()

    groups = plot_df[group_by].tolist()
    x = np.arange(len(groups))

    fig, ax = plt.subplots(figsize=(12, 6))

    # Draw each metric as a full series (all start at 0)
    bar_containers = {}
    for m in METRICS:
        heights = plot_df[m].to_numpy(dtype=float)
        bars = ax.bar(
            x, heights, width=1.0,  # contiguous bars
            alpha=alpha, label=m
        )
        bar_containers[m] = bars

    # Per-group zorder: largest first (low z), smallest last (high z)
    base_z = 2.0
    for i in range(len(groups)):
        entries = [(m, float(plot_df.iloc[i][m]), bar_containers[m][i]) for m in METRICS]
        entries.sort(key=lambda t: t[1], reverse=True)
        for rank, (_, _, rect) in enumerate(entries):
            rect.set_zorder(base_z + rank)

    if logy:
        ax.set_yscale("log")

    # x-axis: keep ALL ticks for continuity, but only show up to N labels
    ax.set_xticks(x)
    ax.set_xticklabels(groups, rotation=90)
    if max_xtick_labels and max_xtick_labels > 0:
        step = max(1, len(groups) // max_xtick_labels)
        for i, lbl in enumerate(ax.get_xticklabels()):
            if i % step != 0:
                lbl.set_visible(False)

    # Remove horizontal gaps at ends; ensure continuous look
    ax.margins(x=0)  # no side margins
    ax.set_xlim(-0.5, len(groups) - 0.5)

    ax.set_xlabel(group_by)
    ax.set_ylabel("Count")
    ax.set_title("TLB Stats")
    ax.legend(title="metric", ncols=len(METRICS))
    ax.grid(axis="y", linestyle="--", alpha=0.3)

    plt.tight_layout()
    plt.savefig(out_png, dpi=200)
    plt.close(fig)

    return {
        "csv_out": csv_out,
        "overlay_plot": out_png,
        "rows": int(len(agg)),
        "topk_plotted": int(len(plot_df)),
    }


def main():
    parser = argparse.ArgumentParser(description="TLB JSON (per_page_translation) → CSV & overlay plot (smallest on top)")
    parser.add_argument("--json", required=True, help="Path to input JSON (must contain 'per_page_translation' array).")
    parser.add_argument("--csv", default="tlb_summary.csv", help="Path to output CSV (all groups).")
    parser.add_argument("--out_png", default="tlb_overlay.png", help="Path to output overlay plot PNG.")
    parser.add_argument("--group_by", default="vpn", help="Group key, e.g., vpn/core/is_instr.")
    parser.add_argument("--alpha", type=float, default=0.6, help="Bar transparency.")
    parser.add_argument("--logy", action="store_true", help="Use log-scale y-axis.")
    parser.add_argument("--topk", type=int, default=50, help="Top-K groups to plot by total (<=0 means all).")
    parser.add_argument("--max_xtick_labels", type=int, default=20, help="Max number of x-axis labels to show (others hidden).")

    args = parser.parse_args()

    res = process_tlb_json_overlay(
        json_path=args.json,
        csv_out=args.csv,
        out_png=args.out_png,
        alpha=args.alpha,
        logy=args.logy,
        topk=args.topk if args.topk > 0 else None,
        group_by=args.group_by,
        max_xtick_labels=args.max_xtick_labels,
    )
    print("Done.")
    for k, v in res.items():
        print(f"{k}: {v}")


if __name__ == "__main__":
    main()
