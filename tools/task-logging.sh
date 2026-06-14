#!/usr/bin/env bash

TASKLOG_OUTPUT_WIDTH=100
TASKLOG_CHECK_WIDTH=81
TASKLOG_STARTED_MS=0
TASKLOG_STEPS=0
TASKLOG_NAME=""
TASKLOG_DIR=""

if [[ -z "${NO_COLOR:-}" ]] && {
  [[ "${FORCE_COLOR:-0}" == "1" ]] || [[ -t 1 && "${TERM:-dumb}" != "dumb" ]]
}; then
  TASKLOG_GREEN=$'\033[32m'
  TASKLOG_CYAN=$'\033[36m'
  TASKLOG_BOLD=$'\033[1m'
  TASKLOG_RESET=$'\033[0m'
else
  TASKLOG_GREEN=""
  TASKLOG_CYAN=""
  TASKLOG_BOLD=""
  TASKLOG_RESET=""
fi

tasklog_detect_width() {
  local detected_width="${COLUMNS:-}"

  if [[ -z "$detected_width" && -t 1 ]] && command -v tput >/dev/null 2>&1; then
    detected_width="$(tput cols 2>/dev/null || true)"
  fi

  if [[ "$detected_width" =~ ^[0-9]+$ ]]; then
    TASKLOG_OUTPUT_WIDTH="$detected_width"
  fi

  if ((TASKLOG_OUTPUT_WIDTH < 80)); then
    TASKLOG_OUTPUT_WIDTH=80
  elif ((TASKLOG_OUTPUT_WIDTH > 160)); then
    TASKLOG_OUTPUT_WIDTH=160
  fi

  TASKLOG_CHECK_WIDTH="$((TASKLOG_OUTPUT_WIDTH - 19))"
}

tasklog_now_ms() {
  local nanoseconds
  nanoseconds="$(date +%s%N)"
  printf '%d\n' "$((nanoseconds / 1000000))"
}

tasklog_duration() {
  local milliseconds="$1"
  printf '%d.%03ds' "$((milliseconds / 1000))" "$((milliseconds % 1000))"
}

tasklog_begin() {
  TASKLOG_NAME="$1"
  TASKLOG_STARTED_MS="$(tasklog_now_ms)"
  TASKLOG_STEPS=0
  TASKLOG_DIR="$(mktemp -d /tmp/gps-reference-task-log.XXXXXX)"
  tasklog_detect_width

  printf '\n%s==> %s%s\n' "$TASKLOG_BOLD$TASKLOG_CYAN" "$TASKLOG_NAME" "$TASKLOG_RESET"
  printf '  %-5s %-*s %8s\n' "TYPE" "$TASKLOG_CHECK_WIDTH" "CHECK" "TIME"
  printf '  %-5s %-*s %8s\n' \
    "-----" \
    "$TASKLOG_CHECK_WIDTH" \
    "$(printf '%*s' "$TASKLOG_CHECK_WIDTH" '' | tr ' ' '-')" \
    "--------"
}

tasklog_step() {
  local kind="$1"
  local label="$2"
  shift 2

  local started_ms
  local finished_ms
  local duration_ms
  local status
  local color="$TASKLOG_GREEN"
  local log_file="$TASKLOG_DIR/step-$TASKLOG_STEPS.log"

  if [[ "$kind" == "BUILD" || "$kind" == "SETUP" ]]; then
    color="$TASKLOG_CYAN"
  fi

  started_ms="$(tasklog_now_ms)"
  if "$@" >"$log_file" 2>&1; then
    finished_ms="$(tasklog_now_ms)"
    duration_ms="$((finished_ms - started_ms))"
    TASKLOG_STEPS="$((TASKLOG_STEPS + 1))"
    printf '  %s%-5s%s %-*s %8s\n' \
      "$color" \
      "$kind" \
      "$TASKLOG_RESET" \
      "$TASKLOG_CHECK_WIDTH" \
      "$label" \
      "$(tasklog_duration "$duration_ms")"
    return
  else
    status=$?
  fi

  finished_ms="$(tasklog_now_ms)"
  duration_ms="$((finished_ms - started_ms))"
  printf '  FAIL  %-*s %8s\n' \
    "$TASKLOG_CHECK_WIDTH" \
    "$label" \
    "$(tasklog_duration "$duration_ms")" >&2
  printf '\n--- failure output: %s ---\n' "$label" >&2
  cat "$log_file" >&2
  printf '%s\n' '--- end failure output ---' >&2
  return "$status"
}

tasklog_end() {
  local detail="${1:-$TASKLOG_STEPS steps}"
  local finished_ms
  local duration_ms

  finished_ms="$(tasklog_now_ms)"
  duration_ms="$((finished_ms - TASKLOG_STARTED_MS))"
  printf '%s<== PASS%s  %s: %s (%s)\n' \
    "$TASKLOG_BOLD$TASKLOG_GREEN" \
    "$TASKLOG_RESET" \
    "$TASKLOG_NAME" \
    "$detail" \
    "$(tasklog_duration "$duration_ms")"
  rm -rf "$TASKLOG_DIR"
  TASKLOG_DIR=""
}
