#!/usr/bin/env bash
# Drop-in importer for a folder of per-exchange zip archives, shaped exactly
# like the Google Drive download folder (CME.zip, NASDAQ.zip, ... side by side,
# no extra structure). Extracts every *.zip into data/<EXCHANGE>/ and runs one
# parser/inserter pass over the whole data dir.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ $# -lt 1 ]; then
  echo "Usage: $0 <folder_with_zips> [data_dir]" >&2
  exit 1
fi

src_dir="$1"
data_dir="${2:-$repo_root/data}"

shopt -s nullglob
zips=("$src_dir"/*.zip)
shopt -u nullglob

if [ ${#zips[@]} -eq 0 ]; then
  echo "error: no .zip files found in $src_dir" >&2
  exit 1
fi

for zip_path in "${zips[@]}"; do
  exchange="$(basename "$zip_path" .zip)"
  target="$data_dir/$exchange"
  mkdir -p "$target"
  echo "[import_folder] extracting $zip_path -> $target"
  python3 -m zipfile -e "$zip_path" "$target"
done

echo "[import_folder] running parser on $data_dir"
"$repo_root/build/ope" insert "$data_dir"
