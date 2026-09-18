#!/usr/bin/env bash
# Flash the current build over WiFi, through the dashboard's /update route.
#
# The device authenticates with a session COOKIE, not HTTP basic auth, so this logs in
# first and reuses the cookie for the upload. The password is read from a file so it
# never lands in shell history, in a process list, or in a chat transcript.
#
#   echo -n 'the-10-digit-password' > secrets/device_password   # secrets/ is git-ignored
#   chmod 600 secrets/device_password
#   tools/ota_update.sh [host]
set -euo pipefail

HOST="${1:-192.168.1.14}"
PASSWORD_FILE="secrets/device_password"
FIRMWARE=".pio/build/esp12e/firmware.bin"
COOKIE_JAR="$(mktemp -t otacookie)"
trap 'rm -f "$COOKIE_JAR"' EXIT

[ -f "$PASSWORD_FILE" ] || { echo "Missing $PASSWORD_FILE" >&2; exit 1; }
[ -f "$FIRMWARE" ] || { echo "Missing $FIRMWARE - run: pio run -e esp12e" >&2; exit 1; }

PASSWORD="$(cat "$PASSWORD_FILE")"

echo "Logging in to $HOST ..."
# --data-binary @- keeps the password off the command line, so it never shows in `ps`.
printf '{"username":"admin","password":"%s"}' "$PASSWORD" |
  curl -sS -f -c "$COOKIE_JAR" -H 'Content-Type: application/json' \
       --data-binary @- "http://$HOST/auth/login" >/dev/null
echo "Logged in."

echo "Uploading $(wc -c < "$FIRMWARE") bytes ..."
curl -sS -f -b "$COOKIE_JAR" -F "firmware=@$FIRMWARE" "http://$HOST/update"
echo
echo "Upload accepted; the device reboots on its own. Verifying it comes back ..."

for _ in $(seq 1 30); do
    if curl -sf -m 3 "http://$HOST/" -o /dev/null; then
        echo "Device is serving again."
        exit 0
    fi
    sleep 2
done
echo "Device did not respond within 60s - check it." >&2
exit 1
