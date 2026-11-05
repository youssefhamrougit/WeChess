#!/usr/bin/env bash
# WeChess - end-to-end smoke test against the real server binary.
#
#   ./scripts/smoke_test.sh [path/to/wechess_server] [path/to/stockfish]
#
# Starts the server on a scratch data directory, imports every PGN in
# tests/data, waits for the review and checks the HTML the WebView would get.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER="${1:-$ROOT/build/wechess_server}"
STOCKFISH="${2:-${WECHESS_STOCKFISH:-}}"
PORT="${PORT:-8099}"
BASE="http://127.0.0.1:$PORT"
DATA="$(mktemp -d)"
LOG="$(mktemp)"
FAILURES=0

args=(--port "$PORT" --data "$DATA")
if [ -n "$STOCKFISH" ]; then
  args+=(--stockfish "$STOCKFISH")
fi

"$SERVER" "${args[@]}" >"$LOG" 2>&1 &
PID=$!
cleanup() {
  kill "$PID" 2>/dev/null || true
  wait "$PID" 2>/dev/null || true
  rm -rf "$DATA"
  if [ "$FAILURES" -ne 0 ]; then
    echo "--- server log ---"
    cat "$LOG"
  fi
  rm -f "$LOG"
}
trap cleanup EXIT

fail() { echo "FAIL: $*"; FAILURES=$((FAILURES + 1)); }
pass() { echo "ok:   $*"; }

for _ in $(seq 1 100); do
  if curl -fsS "$BASE/healthz" >/dev/null 2>&1; then break; fi
  if ! kill -0 "$PID" 2>/dev/null; then
    echo "server exited during startup"
    cat "$LOG"
    exit 1
  fi
  sleep 0.1
done

curl -fsS "$BASE/healthz" >/dev/null || { echo "server never became ready"; cat "$LOG"; exit 1; }
pass "server is ready on $PORT"

home="$(curl -fsS "$BASE/")" || fail "GET /"
case "$home" in
  *WeChess*) pass "home page renders" ;;
  *) fail "home page is missing its branding" ;;
esac
case "$home" in
  *"<script"*) fail "home page contains a script tag" ;;
  *) pass "home page has no script tags" ;;
esac

curl -fsS "$BASE/app.css" | grep -q -- "--board-light" && pass "stylesheet served" || fail "stylesheet missing"

import_pgn() {
  local file="$1"
  local location
  location="$(curl -sS -o /dev/null -w '%{redirect_url}' --data-urlencode "pgn@$file" "$BASE/import")"
  if [ -z "$location" ]; then
    fail "import of $(basename "$file") produced no redirect"
    return 1
  fi
  local id="${location##*/}"
  echo "$id"
}

wait_for_review() {
  local id="$1"
  for _ in $(seq 1 600); do
    local location
    location="$(curl -sS -o /dev/null -w '%{redirect_url}' "$BASE/analysis/$id")"
    if [ -n "$location" ]; then
      echo "$location"
      return 0
    fi
    sleep 0.2
  done
  return 1
}

for file in "$ROOT"/tests/data/*.pgn; do
  name="$(basename "$file")"
  id="$(import_pgn "$file")" || continue
  [ -n "$id" ] || continue
  pass "imported $name as $id"

  page="$(curl -fsS "$BASE/game/$id?ply=0")" || { fail "review page for $name"; continue; }
  squares="$(printf '%s' "$page" | grep -o 'class="sq ' | wc -l | tr -d ' ')"
  if [ "$squares" = "64" ]; then
    pass "  board has 64 squares"
  else
    fail "  board has $squares squares for $name"
  fi
  case "$page" in
    *"<script"*) fail "  review page contains a script tag" ;;
    *) pass "  review page has no script tags" ;;
  esac

  if [ -n "$STOCKFISH" ]; then
    location="$(wait_for_review "$id")"
    if [ -z "$location" ]; then
      fail "  analysis of $name never finished"
      continue
    fi
    review="$(curl -fsS "$BASE${location#http://127.0.0.1:$PORT}")" || { fail "  analysed review"; continue; }
    case "$review" in
      *Accuracy*|*accuracy*) pass "  review shows accuracy" ;;
      *) fail "  review has no accuracy section" ;;
    esac
    case "$review" in
      *"class=\"sq "*) pass "  analysed board rendered" ;;
      *) fail "  analysed board missing" ;;
    esac
  fi
done

if [ "$FAILURES" -ne 0 ]; then
  echo
  echo "$FAILURES smoke test failure(s)"
  exit 1
fi
echo
echo "smoke test passed"
