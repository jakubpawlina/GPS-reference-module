#!/usr/bin/env bash
# run-lint.sh — static analysis for Python and shell code.
#
# Usage:
#   ./tools/run-lint.sh           # exit non-zero on any finding
#
# Tools:
#   ruff check   — Python lint (pyflakes, pycodestyle, isort, pyupgrade)
#   shfmt -l     — shell syntax check

set -euo pipefail
cd "$(dirname "$0")/.."

ERRORS=0

ok() { printf '  \033[32m✔\033[0m  %s\n' "$1"; }
fail() {
	printf '  \033[31m✘\033[0m  %s\n' "$1"
	ERRORS=$((ERRORS + 1))
}
info() { printf '  %s\n' "$1"; }

# ── Python — ruff check ───────────────────────────────────────────────────────

if command -v ruff &>/dev/null; then
	if ruff check service/ tools/*.py; then
		ok "Python (ruff check)"
	else
		fail "Python — fix the issues above or run 'ruff check --fix'"
	fi
else
	info "ruff not found — skipping Python lint (run 'mise install')"
fi

# ── Shell — shfmt syntax check ────────────────────────────────────────────────

SH_FILES=$(find tools -maxdepth 1 -name "*.sh" 2>/dev/null | sort)

if [[ -z "$SH_FILES" ]]; then
	info "No shell files found — skipping"
elif ! command -v shfmt &>/dev/null; then
	info "shfmt not found — skipping shell lint (run 'mise install')"
else
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
fi

# ── Result ────────────────────────────────────────────────────────────────────

echo
if [[ $ERRORS -gt 0 ]]; then
	printf '\033[31mLint failed (%d issue(s))\033[0m\n' "$ERRORS"
	exit 1
else
	printf '\033[32mAll checks passed.\033[0m\n'
fi
