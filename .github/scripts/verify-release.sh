#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
exec python3 "$script_dir/release_artifacts.py" verify "${1:?Usage: verify-release.sh RELEASE_DIR}"
