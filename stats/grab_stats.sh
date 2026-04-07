#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PYTHON_BIN="${PYTHON_BIN:-python3}"
SKIP_FIRST_HALVES="${SKIP_FIRST_HALVES:-10}"
THRESHOLDS="${THRESHOLDS:-1,4,16,32}"
CACHE_FILTER="${CACHE_FILTER:-}"

usage() {
  cat <<'EOF'
Usage:
  stats/grab_stats.sh [options] <file-or-dir>...

Behavior:
  - For each .json:
      1) run hsp_halve_json_to_csv.py
      2) run tlb_json_to_csv.py
      3) run analyze_hsp_hotness.py on the generated *_hsp_halve.csv
  - For each *_hsp_halve.csv:
      run analyze_hsp_hotness.py

Options:
  --skip-first-halves N   Default: 10
  --thresholds LIST       Default: 1,4,16,32
  --cache NAME            Optional cache filter passed to analyze_hsp_hotness.py
  -h, --help              Show this help

Examples:
  stats/grab_stats.sh ./g22sssp_halve1m_promote_on_hit_new.json
  stats/grab_stats.sh ./g22sssp_halve1m_promote_on_hit_new_hsp_halve.csv
  stats/grab_stats.sh --skip-first-halves 10 --thresholds 1,4,16,32 .
EOF
}

run_cmd() {
  printf '+'
  printf ' %q' "$@"
  printf '\n'
  "$@"
}

analyze_hsp_csv() {
  local csv_path="$1"
  local cmd=(
    "${PYTHON_BIN}" "${SCRIPT_DIR}/analyze_hsp_hotness.py"
    --csv "${csv_path}"
    --skip-first-halves "${SKIP_FIRST_HALVES}"
    --thresholds "${THRESHOLDS}"
  )

  if [[ -n "${CACHE_FILTER}" ]]; then
    cmd+=(--cache "${CACHE_FILTER}")
  fi

  run_cmd "${cmd[@]}"
}

process_json() {
  local json_path="$1"
  local hsp_csv="${json_path%.json}_hsp_halve.csv"

  run_cmd "${PYTHON_BIN}" "${SCRIPT_DIR}/hsp_halve_json_to_csv.py" --json "${json_path}"
  run_cmd "${PYTHON_BIN}" "${SCRIPT_DIR}/tlb_json_to_csv.py" --json "${json_path}"

  if [[ -f "${hsp_csv}" ]]; then
    analyze_hsp_csv "${hsp_csv}"
  else
    echo "warning: expected generated file not found: ${hsp_csv}" >&2
  fi
}

process_path() {
  local path="$1"

  if [[ -d "${path}" ]]; then
    local found=0
    local item

    while IFS= read -r -d '' item; do
      found=1
      process_json "${item}"
    done < <(find "${path}" -maxdepth 1 -type f -name '*.json' -print0 | sort -z)

    while IFS= read -r -d '' item; do
      found=1
      analyze_hsp_csv "${item}"
    done < <(find "${path}" -maxdepth 1 -type f -name '*_hsp_halve.csv' -print0 | sort -z)

    if [[ "${found}" -eq 0 ]]; then
      echo "warning: no .json or *_hsp_halve.csv files found in ${path}" >&2
    fi
    return
  fi

  case "${path}" in
    *.json)
      process_json "${path}"
      ;;
    *_hsp_halve.csv)
      analyze_hsp_csv "${path}"
      ;;
    *)
      echo "warning: skipped unsupported input: ${path}" >&2
      ;;
  esac
}

ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-first-halves)
      SKIP_FIRST_HALVES="$2"
      shift 2
      ;;
    --thresholds)
      THRESHOLDS="$2"
      shift 2
      ;;
    --cache)
      CACHE_FILTER="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      ARGS+=("$1")
      shift
      ;;
  esac
done

if [[ "${#ARGS[@]}" -eq 0 ]]; then
  usage
  exit 1
fi

for path in "${ARGS[@]}"; do
  if [[ ! -e "${path}" ]]; then
    echo "warning: path not found: ${path}" >&2
    continue
  fi
  process_path "${path}"
done
