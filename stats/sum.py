import argparse
import json


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize per-page TLB stats from a JSON file.")
    parser.add_argument("json_file", help="Path to JSON with per_page_translation and phases.")
    return parser.parse_args()


def load_json(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def main() -> None:
    args = parse_args()
    data = load_json(args.json_file)

    # ==========  per_page_translation  ==========
    dtlb_acc = dtlb_hit = 0
    itlb_acc = itlb_hit = 0
    stlb_acc = stlb_hit = stlb_ptw = hsp_hit = 0

    for entry in data.get("per_page_translation", []):
        raw = entry["raw"]
        dtlb_acc += raw.get("dtlb_acc", 0)
        dtlb_hit += raw.get("dtlb_hit", 0)
        itlb_acc += raw.get("itlb_acc", 0)
        itlb_hit += raw.get("itlb_hit", 0)
        stlb_acc += raw.get("stlb_acc", 0)
        stlb_hit += raw.get("stlb_hit", 0)
        stlb_ptw += raw.get("stlb_ptw", 0)
        hsp_hit += raw.get("hsp_hit", 0)

    print("=== per_page_translation  ===")
    print(f"DTLB: acc={dtlb_acc}, hit={dtlb_hit}, miss={dtlb_acc - dtlb_hit}")
    print(f"ITLB: acc={itlb_acc}, hit={itlb_hit}, miss={itlb_acc - itlb_hit}")
    computed_ptw = stlb_acc - stlb_hit - hsp_hit
    print(f"STLB: acc={stlb_acc}, hit={stlb_hit}, hsp_hit={hsp_hit}, miss={stlb_acc - stlb_hit}, ptw={computed_ptw}")

    # ========== phase stats==========
    for phase in data.get("phases", []):
        roi = phase["roi"]

        def get_stats(tlb_name):
            tlb = roi.get(tlb_name, {})
            load_hit = sum(tlb.get("LOAD", {}).get("hit", []))
            load_miss = sum(tlb.get("LOAD", {}).get("miss", []))
            acc = load_hit + load_miss
            return acc, load_hit, load_miss

        dtlb_acc, dtlb_hit, dtlb_miss = get_stats("cpu0_DTLB")
        itlb_acc, itlb_hit, itlb_miss = get_stats("cpu0_ITLB")
        stlb_acc, stlb_hit, stlb_miss = get_stats("cpu0_STLB")

        print("\n=== Phase:", phase["name"], "===")
        print(f"DTLB: acc={dtlb_acc}, hit={dtlb_hit}, miss={dtlb_miss}")
        print(f"ITLB: acc={itlb_acc}, hit={itlb_hit}, miss={itlb_miss}")
        print(f"STLB: acc={stlb_acc}, hit={stlb_hit}, miss={stlb_miss}")


if __name__ == "__main__":
    main()