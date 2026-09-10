#!/usr/bin/env bash
set -euo pipefail
repo=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}

if [[ -n "${K230_BURNING_REVISION:-}" ]]; then
    revision=$K230_BURNING_REVISION
elif [[ "${GITHUB_REF_TYPE:-}" == tag && -n "${GITHUB_REF_NAME:-}" ]]; then
    revision=$GITHUB_REF_NAME
elif [[ -n "${GITHUB_SHA:-}" ]]; then
    revision=${GITHUB_SHA:0:12}
elif revision=$(git -C "$repo" describe --tags --exact-match HEAD 2>/dev/null); then
    :
else
    revision=$(git -C "$repo" rev-parse --short=12 HEAD 2>/dev/null || printf unknown)
fi

if git -C "$repo" rev-parse --verify HEAD >/dev/null 2>&1 && ! git -C "$repo" diff --quiet HEAD --; then
    echo "Warning: tracked workspace changes exist; artifact names use the revision label without a dirty suffix." >&2
fi
printf '%s' "$revision" | LC_ALL=C tr -c 'A-Za-z0-9._+-' '-'
printf '\n'
