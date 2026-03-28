#!/usr/bin/env bash
# Load factotum keys from an aescbc-encrypted file.
#
# Called by run-with-env.sh via P9P_POST_START_HOOK after the factotum
# socket appears. Retrieves the aescbc passphrase from macOS Keychain
# (service: plan9port-factotum) so no manual interaction is required.
#
# First-time setup: run  mk -f $PLAN9/mac/mkfile factotum-keys-init

PLAN9=${PLAN9:-/usr/local/plan9}
KEYFILE="${FACTOTUM_KEYFILE:-$HOME/lib/factotum}"
KEYCHAIN_SERVICE="plan9port-factotum"

if [[ ! -f "$KEYFILE" ]]; then
	exit 0
fi

pw=$(security find-generic-password -s "$KEYCHAIN_SERVICE" -a "$USER" -w 2>/dev/null)
if [[ -z "$pw" ]]; then
	# Keychain entry not found — factotum starts without pre-loaded keys.
	# Run:  mk -f $PLAN9/mac/mkfile factotum-keys-init
	exit 0
fi

# Write the password to a private temp file for fd 3.
# aescbc -i reads the passphrase from fd 3 (not stdin, which carries the ciphertext).
tmp=$(mktemp)
chmod 600 "$tmp"
printf '%s\n' "$pw" > "$tmp"

"$PLAN9/bin/aescbc" -i -d < "$KEYFILE" 3<"$tmp" | \
	grep -v '^[[:space:]]*$' | \
	"$PLAN9/bin/9p" write -l factotum/ctl 2>/dev/null || true

rm -f "$tmp"
