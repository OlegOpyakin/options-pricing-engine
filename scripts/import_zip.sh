#!/usr/bin/env bash
# Extract one exchange zip archive (e.g. CME.zip) into data/<EXCHANGE>/
# and run the parser/inserter on it.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ $# -lt 1 ]; then
  echo "Usage: $0 <archive.zip> [data_dir]" >&2
  exit 1
fi

zip_path="$1"
data_dir="${2:-$repo_root/data}"

if [ ! -f "$zip_path" ]; then
  echo "error: file not found: $zip_path" >&2
  exit 1
fi

exchange="$(basename "$zip_path" .zip)"
target="$data_dir/$exchange"

mkdir -p "$target"
echo "[import_zip] extracting $zip_path -> $target"
python3 -m zipfile -e "$zip_path" "$target"

echo "[import_zip] running parser on $target"
"$repo_root/build/ope" insert "$target"
