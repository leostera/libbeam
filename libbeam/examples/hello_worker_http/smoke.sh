#!/usr/bin/env bash
# Prove that curl reaches the unchanged HelloWorker via a real libbeam Isolate.
set -euo pipefail

if [[ $# -ne 3 && $# -ne 4 ]]; then
  echo "Usage: smoke.sh PATH_TO_BRIDGE PATH_TO_HELLO_WORKER.bundle PATH_TO_MIDGARD_HELLO_WORKER_MIX_PROJECT [MEMORY_MiB]" >&2
  exit 2
fi
bridge=$1
bundle=$2
mix_project=$3
oracle_script=$(cd -- "$(dirname -- "$0")" && pwd)/otp_reference.exs
temp=$(mktemp -d)
pid=
cleanup() {
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
  rm -rf "$temp"
}
trap cleanup EXIT

"$bridge" --self-check
"$bridge" "$bundle" 0 2 "${4:-256}" > "$temp/bridge.log" 2>&1 &
pid=$!
# A full 381-module verifier run can take over a minute on a cold worktree.
for ((i = 0; i < 1800; ++i)); do
  if grep -Eq '^READY http://127[.]0[.]0[.]1:[0-9]+$' "$temp/bridge.log"; then break; fi
  if ! kill -0 "$pid" 2>/dev/null; then
    echo "Isolate did not admit the full artifact:" >&2
    grep -E 'whole-group admission|bridge failed|world load limit|stage=|Isolate closed|memory_current' "$temp/bridge.log" >&2 || true
    exit 1
  fi
  sleep 0.1
done
if ! grep -Eq '^READY http://127[.]0[.]0[.]1:[0-9]+$' "$temp/bridge.log"; then
  echo "Timed out after 180 seconds waiting for Isolate admission" >&2
  grep -E 'whole-group admission|bridge failed|stage=|Isolate closed|memory_current' "$temp/bridge.log" >&2 || true
  exit 1
fi
port=$(grep '^READY http://' "$temp/bridge.log" | head -1 | cut -d: -f3)
url="http://127.0.0.1:$port"

curl --noproxy '*' --silent --show-error --max-time 20 --dump-header "$temp/get.headers" --output "$temp/get.body" "$url/hello"
printf 'Hello, world!\n' > "$temp/get.expected"
grep -qi '^HTTP/1\.1 200 OK' "$temp/get.headers"
grep -qi '^Content-Type: text/plain; charset=utf-8' "$temp/get.headers"
cmp "$temp/get.expected" "$temp/get.body"
printf 'GET /hello: '; head -1 "$temp/get.headers" | tr -d '\r\n'; printf ' body='; od -An -tx1 "$temp/get.body"

curl --noproxy '*' --silent --show-error --max-time 20 --dump-header "$temp/post.headers" --output "$temp/post.body" \
  -H 'Content-Type: application/json' --data-binary '{"name":"Leo"}' "$url/json"
printf '%s' '{"greeting":"Hello, Leo!\n"}' > "$temp/post.expected"
grep -qi '^HTTP/1\.1 200 OK' "$temp/post.headers"
grep -qi '^Content-Type: application/json; charset=utf-8' "$temp/post.headers"
cmp "$temp/post.expected" "$temp/post.body"
printf 'POST /json: '; head -1 "$temp/post.headers" | tr -d '\r\n'; printf ' body='; od -An -tx1 "$temp/post.body"

wait "$pid"
pid=
grep -q 'Isolate closed=1 memory_current=0' "$temp/bridge.log"
(cd "$mix_project" && mix run --no-start "$oracle_script") > "$temp/otp-oracle.log"
get_otp=$(grep '^GET=' "$temp/otp-oracle.log")
post_otp=$(grep '^POST=' "$temp/otp-oracle.log")
get_isolate=$(grep '^GET /hello -> .* wire_response_hex=' "$temp/bridge.log")
post_isolate=$(grep '^POST /json -> .* wire_response_hex=' "$temp/bridge.log")
[[ "${get_otp#GET=}" == "${get_isolate##*wire_response_hex=}" ]]
[[ "${post_otp#POST=}" == "${post_isolate##*wire_response_hex=}" ]]
printf 'GET OTP/Isolate wire bytes: %s\n' "${get_otp#GET=}"
printf 'POST OTP/Isolate wire bytes: %s\n' "${post_otp#POST=}"
echo 'PASS: curl reached the real Isolate; both wire responses match ordinary OTP; memory reclaimed'
