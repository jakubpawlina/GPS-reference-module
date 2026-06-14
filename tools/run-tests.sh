#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd -P)"
BUILD_DIR="$ROOT/tests/.build"
FIRMWARE_SRC="$ROOT/firmware/gps_reference_module/src"

mkdir -p "$BUILD_DIR"
RUN_DIR="$(mktemp -d "$BUILD_DIR/run.XXXXXX")"
trap 'rm -rf "$RUN_DIR"' EXIT

UNIT_TEST_BIN="$RUN_DIR/test_firmware_unit"
INTEGRATION_TEST_BIN="$RUN_DIR/test_firmware_integration"
CHIP_TEST_BIN="$RUN_DIR/test_wokwi_gps_chip"
SERVICE_PYTHON="$ROOT/.venv/bin/python"

readonly C_WARNINGS=(-Wall -Wextra -Werror -pedantic)
# The comma is part of the gcc flag, not an array separator.
# shellcheck disable=SC2054
readonly CXX_SANITIZERS=(-fsanitize=address,undefined -fno-omit-frame-pointer)

SUITE_NAME=""
SUITE_STARTED_MS=0
SUITE_STEPS=0
SUITE_TESTS=0
ALL_STARTED_MS=0
ALL_STEPS=0
ALL_TESTS=0
OUTPUT_WIDTH=100
CHECK_WIDTH=82

if [[ -z "${NO_COLOR:-}" ]] && {
	[[ "${FORCE_COLOR:-0}" == "1" ]] || [[ -t 1 && "${TERM:-dumb}" != "dumb" ]]
}; then
	COLOR_GREEN=$'\033[32m'
	COLOR_CYAN=$'\033[36m'
	COLOR_BOLD=$'\033[1m'
	COLOR_RESET=$'\033[0m'
else
	COLOR_GREEN=""
	COLOR_CYAN=""
	COLOR_BOLD=""
	COLOR_RESET=""
fi

detect_output_width() {
	local detected_width="${COLUMNS:-}"

	if [[ -z "$detected_width" && -t 1 ]] && command -v tput >/dev/null 2>&1; then
		detected_width="$(tput cols 2>/dev/null || true)"
	fi

	if [[ "$detected_width" =~ ^[0-9]+$ ]]; then
		OUTPUT_WIDTH="$detected_width"
	fi

	if ((OUTPUT_WIDTH < 80)); then
		OUTPUT_WIDTH=80
	elif ((OUTPUT_WIDTH > 160)); then
		OUTPUT_WIDTH=160
	fi

	# Fixed width is 17 columns: indent (2), type (5), separators (2), time (8).
	# Keep two additional columns free because terminal wrappers may report the
	# full width while reserving the final cells for wrapping or UI decoration.
	CHECK_WIDTH="$((OUTPUT_WIDTH - 19))"
}

detect_output_width

usage() {
	cat <<'EOF'
Usage: ./tools/run-tests.sh <suite>

Suites:
  unit          Test pure firmware parsing, state, framing, and presentation
  integration   Test the complete firmware runtime with simulated peripherals
  simulation    Test Wokwi assets, generation, and custom GPS chip behavior
  service       Test Raspberry Pi storage, API, and lifecycle behavior
  all           Run every host-side test suite
EOF
}

require_tool() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing required tool: $1" >&2
		echo "Run 'mise install' first, or install it manually." >&2
		exit 1
	fi
}

now_ms() {
	local nanoseconds
	nanoseconds="$(date +%s%N)"
	printf '%d\n' "$((nanoseconds / 1000000))"
}

format_duration() {
	local milliseconds="$1"
	printf '%d.%03ds' "$((milliseconds / 1000))" "$((milliseconds % 1000))"
}

begin_suite() {
	SUITE_NAME="$1"
	SUITE_STARTED_MS="$(now_ms)"
	SUITE_STEPS=0
	SUITE_TESTS=0
	printf '\n%s==> %s%s\n' "$COLOR_BOLD$COLOR_CYAN" "$SUITE_NAME" "$COLOR_RESET"
	printf '  %-5s %-*s %8s\n' "TYPE" "$CHECK_WIDTH" "CHECK" "TIME"
	printf '  %-5s %-*s %8s\n' \
		"-----" \
		"$CHECK_WIDTH" \
		"$(printf '%*s' "$CHECK_WIDTH" '' | tr ' ' '-')" \
		"--------"
}

print_step() {
	local kind="$1"
	local label="$2"
	local duration="$3"
	local color="$COLOR_GREEN"
	if [[ "$kind" == "BUILD" ]]; then
		color="$COLOR_CYAN"
	fi
	printf '  %s%-5s%s %-*s %8s\n' \
		"$color" \
		"$kind" \
		"$COLOR_RESET" \
		"$CHECK_WIDTH" \
		"$label" \
		"$duration"
}

print_check_description() {
	local description="$1"
	local description_width="$((OUTPUT_WIDTH - 16))"
	local line
	local first_line=true

	while IFS= read -r line; do
		if $first_line; then
			printf '        %sCHECK%s %s\n' "$COLOR_CYAN" "$COLOR_RESET" "$line"
			first_line=false
		else
			printf '              %s\n' "$line"
		fi
	done < <(
		printf '%s\n' "$description" |
			fold -s -w "$description_width" |
			sed 's/[[:space:]]*$//'
	)
}

run_step() {
	local kind="$1"
	local test_count="$2"
	local label="$3"
	shift 3

	local started_ms
	local finished_ms
	local duration_ms
	local log_file="$RUN_DIR/step-$SUITE_STEPS.log"
	started_ms="$(now_ms)"

	if "$@" >"$log_file" 2>&1; then
		finished_ms="$(now_ms)"
		duration_ms="$((finished_ms - started_ms))"
		SUITE_STEPS="$((SUITE_STEPS + 1))"
		SUITE_TESTS="$((SUITE_TESTS + test_count))"
		print_step "$kind" "$label" "$(format_duration "$duration_ms")"
		return
	else
		local status=$?
	fi

	finished_ms="$(now_ms)"
	duration_ms="$((finished_ms - started_ms))"
	printf '  FAIL  %-*s %8s\n' "$CHECK_WIDTH" "$label" "$(format_duration "$duration_ms")" >&2
	printf '\n--- failure output: %s ---\n' "$label" >&2
	cat "$log_file" >&2
	printf '%s\n' '--- end failure output ---' >&2
	return "$status"
}

run_listed_step() {
	local kind="$1"
	local test_count="$2"
	local label="$3"
	shift 3

	local started_ms
	local finished_ms
	local duration_ms
	local log_file="$RUN_DIR/step-$SUITE_STEPS.log"
	started_ms="$(now_ms)"

	if "$@" >"$log_file" 2>&1; then
		finished_ms="$(now_ms)"
		duration_ms="$((finished_ms - started_ms))"
		SUITE_STEPS="$((SUITE_STEPS + 1))"
		SUITE_TESTS="$((SUITE_TESTS + test_count))"
		print_step "$kind" "$label" "$(format_duration "$duration_ms")"
		while IFS=$'\t' read -r result test_name test_description; do
			if [[ "$result" == "PASS" && -n "$test_name" ]]; then
				printf '        %sPASS%s  %s\n' "$COLOR_GREEN" "$COLOR_RESET" "$test_name"
				if [[ -n "$test_description" ]]; then
					print_check_description "$test_description"
				fi
			fi
		done <"$log_file"
		return
	else
		local status=$?
	fi

	finished_ms="$(now_ms)"
	duration_ms="$((finished_ms - started_ms))"
	printf '  FAIL  %-*s %8s\n' "$CHECK_WIDTH" "$label" "$(format_duration "$duration_ms")" >&2
	printf '\n--- failure output: %s ---\n' "$label" >&2
	cat "$log_file" >&2
	printf '%s\n' '--- end failure output ---' >&2
	return "$status"
}

end_suite() {
	local finished_ms
	local duration_ms
	finished_ms="$(now_ms)"
	duration_ms="$((finished_ms - SUITE_STARTED_MS))"
	ALL_STEPS="$((ALL_STEPS + SUITE_STEPS))"
	ALL_TESTS="$((ALL_TESTS + SUITE_TESTS))"
	printf '%s<== PASS%s  %s: %d tests, %d steps (%s)\n' \
		"$COLOR_BOLD$COLOR_GREEN" \
		"$COLOR_RESET" \
		"$SUITE_NAME" \
		"$SUITE_TESTS" \
		"$SUITE_STEPS" \
		"$(format_duration "$duration_ms")"
}

run_sanitized() {
	ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" "$@"
}

run_unit_tests() {
	require_tool g++
	begin_suite "Firmware unit tests"

	run_step BUILD 0 "Compile C++17 (-Werror, ASan, UBSan)" \
		g++ \
		-std=c++17 \
		"${C_WARNINGS[@]}" \
		"${CXX_SANITIZERS[@]}" \
		-I"$FIRMWARE_SRC" \
		"$ROOT/tests/firmware/test_gps_processing.cpp" \
		"$FIRMWARE_SRC/gps_processing.cpp" \
		"$FIRMWARE_SRC/nmea_stream_framer.cpp" \
		"$FIRMWARE_SRC/status_presentation.cpp" \
		-o "$UNIT_TEST_BIN"

	run_listed_step PASS 11 "Execute firmware unit cases" \
		run_sanitized "$UNIT_TEST_BIN"

	end_suite
}

run_integration_tests() {
	require_tool g++
	begin_suite "Firmware runtime integration tests"

	run_step BUILD 0 "Compile runtime + fakes (-Werror, ASan, UBSan)" \
		g++ \
		-std=c++17 \
		"${C_WARNINGS[@]}" \
		"${CXX_SANITIZERS[@]}" \
		-I"$ROOT/tests/integration/fakes" \
		-I"$FIRMWARE_SRC" \
		"$ROOT/tests/integration/fakes/fake_arduino.cpp" \
		"$ROOT/tests/integration/test_firmware_runtime.cpp" \
		"$FIRMWARE_SRC/firmware_runtime.cpp" \
		"$FIRMWARE_SRC/gps_processing.cpp" \
		"$FIRMWARE_SRC/nmea_stream_framer.cpp" \
		"$FIRMWARE_SRC/oled_display.cpp" \
		"$FIRMWARE_SRC/serial_json_reporter.cpp" \
		"$FIRMWARE_SRC/status_led_controller.cpp" \
		"$FIRMWARE_SRC/status_presentation.cpp" \
		-o "$INTEGRATION_TEST_BIN"

	local scenario
	local scenario_details
	local scenario_check
	while IFS=';' read -r scenario scenario_details scenario_check; do
		run_step PASS 1 "$scenario_details" run_sanitized "$INTEGRATION_TEST_BIN" "$scenario"
		print_check_description "$scenario_check"
	done <<'EOF'
no-data;NO_GPS_DATA | OLED=NO DATA | LED E=1 D=0 W=0 OK=0;No UART data produces the expected serial, display, and error indication.
no-fix;NO_FIX | OLED=NO FIX | LED E=1 D=1 W=0 OK=0;Valid NMEA without a fix enables data and error indications.
fix-2d;DEGRADED_2D | OLED=WARN 2D | LED E=0 D=1 W=1 OK=0;A 2D position is accepted but altitude remains degraded.
low-sat;DEGRADED_LOW_SAT | OLED=LOW SAT | LED E=0 D=1 W=1 OK=0;A 3D fix below the satellite threshold remains a warning.
ok;REFERENCE_OK | OLED=OK | LED E=0 D=1 W=0 OK=1;A healthy 3D fix drives consistent serial, OLED, and LED outputs.
EOF

	end_suite
}

run_simulation_tests() {
	require_tool cc
	require_tool git
	require_tool python3
	begin_suite "Wokwi simulation contract tests"

	run_step BUILD 0 "Compile custom GPS chip test (C11, -Werror)" \
		cc \
		-std=c11 \
		"${C_WARNINGS[@]}" \
		"$ROOT/tests/simulation/test_neo_m8n_chip.c" \
		-o "$CHIP_TEST_BIN"

	run_listed_step PASS 3 "Execute custom GPS chip cases" \
		"$CHIP_TEST_BIN"
	run_listed_step PASS 5 "Execute project generator contract cases" \
		run_python_simulation_tests

	local generated_dir
	generated_dir="$(mktemp -d /tmp/gps-reference-wokwi-validation.XXXXXX)"
	trap 'rm -rf "$generated_dir"' RETURN

	run_step PASS 1 "Generate a clean Wokwi project" \
		python3 "$ROOT/tools/generate-wokwi-project.py" --output-dir "$generated_dir"
	print_check_description \
		"Builds a disposable project from tracked firmware and simulation sources."
	run_step PASS 1 "Validate generated project structure and wiring" \
		python3 "$ROOT/tools/validate-wokwi-project.py" --project-dir "$generated_dir"
	print_check_description \
		"Confirms generated files are complete, current, correctly wired, and free of ignored output."

	rm -rf "$generated_dir"
	trap - RETURN
	end_suite
}

run_python_simulation_tests() {
	python3 "$ROOT/tests/simulation/test_wokwi_generator.py"
}

run_service_tests() {
	if [[ ! -x "$SERVICE_PYTHON" ]]; then
		echo "Missing service environment: $SERVICE_PYTHON" >&2
		echo "Run 'mise run service:bootstrap' first." >&2
		exit 1
	fi

	begin_suite "Raspberry Pi service tests"
	run_listed_step PASS 37 "Execute async service cases" \
		"$SERVICE_PYTHON" "$ROOT/tests/service/test_service.py"
	end_suite
}

run_all_tests() {
	ALL_STARTED_MS="$(now_ms)"
	run_unit_tests
	run_integration_tests
	run_simulation_tests
	run_service_tests

	local finished_ms
	local duration_ms
	finished_ms="$(now_ms)"
	duration_ms="$((finished_ms - ALL_STARTED_MS))"
	printf '\n%s=== PASS%s  All host-side tests: %d tests, %d steps (%s)\n' \
		"$COLOR_BOLD$COLOR_GREEN" \
		"$COLOR_RESET" \
		"$ALL_TESTS" \
		"$ALL_STEPS" \
		"$(format_duration "$duration_ms")"
}

main() {
	if [[ $# -ne 1 ]]; then
		usage
		exit 1
	fi

	case "$1" in
	unit) run_unit_tests ;;
	integration) run_integration_tests ;;
	simulation) run_simulation_tests ;;
	service) run_service_tests ;;
	all) run_all_tests ;;
	*)
		usage
		exit 1
		;;
	esac
}

main "$@"
