#!/usr/bin/env bash
# run-format.sh — format or check all source code in the project.
#
# Usage:
#   ./tools/run-format.sh           # format in place
#   ./tools/run-format.sh --check   # exit non-zero if any file needs reformatting
#
# Formatters used:
#   ruff format   — Python (service/, tools/*.py, tests/)
#   clang-format  — C++ (firmware/, tests/firmware/, tests/integration/)
#   shfmt         — Shell (tools/*.sh)

set -euo pipefail
cd "$(dirname "$0")/.."

# ── Argument parsing ──────────────────────────────────────────────────────────

CHECK=false
if [[ "${1:-}" == "--check" ]]; then
	CHECK=true
fi

ERRORS=0

# ── Helpers ───────────────────────────────────────────────────────────────────

ok() { printf '  \033[32m✔\033[0m  %s\n' "$1"; }
fail() {
	printf '  \033[31m✘\033[0m  %s\n' "$1"
	ERRORS=$((ERRORS + 1))
}
info() { printf '  %s\n' "$1"; }

# ── Python — ruff ─────────────────────────────────────────────────────────────

if command -v ruff &>/dev/null; then
	if $CHECK; then
		if ruff format --check service/ tools/*.py tests/ 2>/dev/null; then
			ok "Python (ruff format --check)"
		else
			fail "Python — run 'mise run format' to fix"
		fi
	else
		ruff format service/ tools/*.py tests/ 2>/dev/null
		ok "Python (ruff format)"
	fi
else
	info "ruff not found — skipping Python format (run 'mise install')"
fi

# ── C++ — clang-format ────────────────────────────────────────────────────────

CPP_FILES=$(find firmware/gps_reference_module/src tests/firmware tests/integration \
	\( -name "*.cpp" -o -name "*.h" \) 2>/dev/null | sort)

if [[ -z "$CPP_FILES" ]]; then
	info "No C++ files found — skipping"
elif ! command -v clang-format &>/dev/null; then
	info "clang-format not found — skipping C++ format (sudo apt install clang-format)"
elif $CHECK; then
	NEEDS_FORMAT=()
	while IFS= read -r f; do
		if ! clang-format --dry-run --Werror "$f" &>/dev/null; then
			NEEDS_FORMAT+=("$f")
		fi
	done <<<"$CPP_FILES"
	if [[ ${#NEEDS_FORMAT[@]} -eq 0 ]]; then
		ok "C++ (clang-format --check)"
	else
		fail "C++ — run 'mise run format' to fix: ${NEEDS_FORMAT[*]}"
	fi
else
	# shellcheck disable=SC2086
	echo "$CPP_FILES" | xargs clang-format -i
	ok "C++ (clang-format)"
fi

# ── Shell — shfmt ─────────────────────────────────────────────────────────────

SH_FILES=$(find tools -maxdepth 1 -name "*.sh" 2>/dev/null | sort)

if [[ -z "$SH_FILES" ]]; then
	info "No shell files found — skipping"
elif ! command -v shfmt &>/dev/null; then
	info "shfmt not found — skipping shell format (run 'mise install')"
elif $CHECK; then
	if echo "$SH_FILES" | xargs shfmt -d 2>&1 | grep -q .; then
		fail "Shell — run 'mise run format' to fix"
	else
		ok "Shell (shfmt --check)"
	fi
else
	echo "$SH_FILES" | xargs shfmt -w
	ok "Shell (shfmt)"
fi

# ── Result ────────────────────────────────────────────────────────────────────

echo
if [[ $ERRORS -gt 0 ]]; then
	printf '\033[31mFormatting check failed (%d issue(s))\033[0m\n' "$ERRORS"
	exit 1
else
	if $CHECK; then
		printf '\033[32mAll files are correctly formatted.\033[0m\n'
	else
		printf '\033[32mFormatting complete.\033[0m\n'
	fi
fi
