#!/bin/bash
set -euo pipefail

usage() {
  echo "Usage: $0 {tx|rx} [serial-port] [--erase]" >&2
  exit 2
}

case "${1-}" in
  tx|rx) role=$1; shift ;;
  *) usage ;;
esac
erase=false
port=""
for arg in "$@"; do
  case "$arg" in
    --erase) erase=true ;;
    -*) usage ;;
    *) [[ -z "$port" && -n "$arg" ]] || usage; port=$arg ;;
  esac
done
set --
if [[ -n "$port" ]]; then
  set -- --upload-port "$port"
fi

command -v pio >/dev/null || { echo "PlatformIO is required (pio)." >&2; exit 1; }
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if "$erase"; then
  echo "Building $role firmware before erasing all flash (including profiles, pairing and Wi-Fi settings)."
  pio run -d "$root/firmware" -e "thinknode_m5_$role"
  pio run -d "$root/firmware" -e "thinknode_m5_$role" -t erase "$@"
  echo "Flashing $role firmware with fresh settings."
else
  echo "Flashing $role firmware. Existing settings are preserved; firmware selects the role."
fi
exec pio run -d "$root/firmware" -e "thinknode_m5_$role" -t upload "$@"
