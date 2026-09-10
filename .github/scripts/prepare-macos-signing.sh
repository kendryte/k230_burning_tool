#!/usr/bin/env bash
# Source this script in the signing step; never enable shell tracing here.
set -euo pipefail

MACOS_SIGN_IDENTITY=$(printf '%s' "${MACOS_SIGN_IDENTITY:-}" | tr -d '\r\n')
MACOS_KEYCHAIN=$(printf '%s' "${MACOS_KEYCHAIN:-}" | tr -d '\r\n')
MACOS_NOTARY_PROFILE=$(printf '%s' "${MACOS_NOTARY_PROFILE:-}" | tr -d '\r\n')
: "${MACOS_SIGN_IDENTITY:?Set MACOS_SIGN_IDENTITY in macos-signing}"
: "${MACOS_KEYCHAIN:?Set an explicit MACOS_KEYCHAIN path in macos-signing}"
: "${MACOS_NOTARY_PROFILE:?Set MACOS_NOTARY_PROFILE in macos-signing}"
: "${MACOS_KEYCHAIN_PASSWORD:?Set MACOS_KEYCHAIN_PASSWORD in macos-signing}"
if [[ ! -f "$MACOS_KEYCHAIN" ]]; then
    echo "The configured signing keychain does not exist" >&2
    exit 1
fi

security unlock-keychain -p "$MACOS_KEYCHAIN_PASSWORD" "$MACOS_KEYCHAIN" >/dev/null
security set-key-partition-list -S apple-tool:,apple:,codesign: \
    -s -k "$MACOS_KEYCHAIN_PASSWORD" "$MACOS_KEYCHAIN" >/dev/null
identities=$(security find-identity -v -p codesigning "$MACOS_KEYCHAIN")
if ! grep -F -- "$MACOS_SIGN_IDENTITY" <<<"$identities" >/dev/null; then
    echo "The configured signing identity was not found" >&2
    exit 1
fi
xcrun notarytool history --keychain-profile "$MACOS_NOTARY_PROFILE" \
    --keychain "$MACOS_KEYCHAIN" --output-format json >/dev/null
export MACOS_SIGN_IDENTITY MACOS_KEYCHAIN MACOS_NOTARY_PROFILE
unset identities
