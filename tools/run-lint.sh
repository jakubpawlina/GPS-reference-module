#!/usr/bin/env bash
# run-lint.sh — static analysis for Python and shell code.
#
# Usage:
#   ./tools/run-lint.sh           # exit non-zero on any finding
#   ./tools/run-lint.sh --fix     # auto-fix what ruff can fix
#
# Tools:
#   ruff check   — Python lint (pyflakes, pycodestyle, isort, pyupgrade)
#   shfmt -l     — shell syntax check

set -Eeuo pipefail
cd "$(dirname "$0")/.."

FIX=false
if [[ "${1:-}" == "--fix" ]]; then
	FIX=true
fi

ERRORS=0

ok() { printf '  \033[32m✔\033[0m  %s\n' "$1"; }
fail() {
	printf '  \033[31m✘\033[0m  %s\n' "$1"
	ERRORS=$((ERRORS + 1))
}
info() { printf '  %s\n' "$1"; }

# ── Python — ruff check ───────────────────────────────────────────────────────

if command -v ruff &>/dev/null; then
	if $FIX; then
		if ruff check --fix service/ tools/*.py tests/; then
			ok "Python (ruff check --fix)"
		else
			fail "Python — some issues could not be auto-fixed"
		fi
	elif ruff check service/ tools/*.py tests/; then
		ok "Python (ruff check)"
	else
		fail "Python — run 'mise run lint:fix' to auto-fix"
	fi
else
	info "ruff not found — skipping Python lint (run 'mise install')"
fi

# ── Shell — shellcheck static analysis ───────────────────────────────────────

SH_FILES=$(find tools -maxdepth 1 -name "*.sh" 2>/dev/null | sort)

if [[ -z "$SH_FILES" ]]; then
	info "No shell files found — skipping"
elif command -v shellcheck &>/dev/null; then
	# Real static analysis for shell bugs
	if echo "$SH_FILES" | xargs shellcheck -S warning; then
		ok "Shell (shellcheck)"
	else
		fail "Shell — shellcheck found issues"
	fi
else
	info "shellcheck not found — skipping shell static analysis (run 'mise install')"
fi

# ── Shell — shfmt formatting check ──────────────────────────────────────────

if [[ -n "$SH_FILES" ]] && command -v shfmt &>/dev/null; then
	BAD=()
	while IFS= read -r f; do
		if ! shfmt -l "$f" | grep -q .; then
			: # correctly formatted = no output = ok
		else
			BAD+=("$f")
		fi
	done <<<"$SH_FILES"
	if [[ ${#BAD[@]} -eq 0 ]]; then
		ok "Shell (shfmt -l)"
	else
		fail "Shell — run 'mise run format' to fix: ${BAD[*]}"
	fi
elif [[ -n "$SH_FILES" ]]; then
	info "shfmt not found — skipping shell format check (run 'mise install')"
fi

# ── Result ────────────────────────────────────────────────────────────────────

echo
if [[ $ERRORS -gt 0 ]]; then
	printf '\033[31mLint failed (%d issue(s))\033[0m\n' "$ERRORS"
	exit 1
else
	printf '\033[32mAll checks passed.\033[0m\n'
fi
