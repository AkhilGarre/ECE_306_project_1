#!/usr/bin/env bash
# =========================================================================
# test.sh -- automated verification for harness.c (ECE 309 Project 1)
#
# Runs with NO human interaction. It:
#   1. builds harness.c with warnings-as-signal
#   2. pipes scripted sessions in and checks the responses
#   3. verifies the 5-turn context window evicts the oldest turn
#   4. runs a memory check (valgrind if available, else ASan/UBSan build)
#
# Usage:  bash test.sh
# Exit:   0 = ALL TESTS PASSED, 1 = a check failed
# =========================================================================

set -u
SRC="harness.c"
BIN="./harness"
CC="${CC:-gcc}"
FAILED=0

say()  { printf '%s\n' "$*"; }
pass() { printf '  PASS  %s\n' "$*"; }
fail() { printf '  FAIL  %s\n' "$*"; FAILED=1; }

# --- check: $1 = description, $2 = haystack, $3 = needle (must be present) ---
expect_contains() {
    case "$2" in
        *"$3"*) pass "$1" ;;
        *)      fail "$1 (expected to find: '$3')" ;;
    esac
}

# --- check: $1 = description, $2 = haystack, $3 = needle (must be absent) ---
expect_absent() {
    case "$2" in
        *"$3"*) fail "$1 (did not expect to find: '$3')" ;;
        *)      pass "$1" ;;
    esac
}

# =========================================================================
say "== 1. Build =="
# -Werror turns any warning into a build failure: the spec requires a clean
# compile under -Wall -Wextra.
if $CC -std=c11 -Wall -Wextra -Werror -g "$SRC" -o harness 2> build.log; then
    pass "compiles clean with -Wall -Wextra -Werror"
else
    fail "compilation failed / had warnings:"
    sed 's/^/        /' build.log
    say ""
    say "TESTS ABORTED (no binary to test)."
    exit 1
fi

# Also prove the bare grader command works.
if $CC "$SRC" -o harness_plain 2> /dev/null; then
    pass "compiles with bare '$CC $SRC -o harness'"
else
    fail "bare compile command failed"
fi
rm -f harness_plain

# =========================================================================
say ""
say "== 2. Core loop / mock model / tool =="

OUT=$(printf 'Well hello there\ncalc 6 / 2\ncalc 1 / 0\ncalc 2 * 21\nwhat is the weather\nexit\n' | "$BIN")

expect_contains "greeting fires on 'hello'"        "$OUT" "Hello! I am a mock model."
expect_contains "calculator: 6 / 2 = 3"            "$OUT" "= 3"
expect_contains "calculator: 2 * 21 = 42"          "$OUT" "= 42"
expect_contains "calculator: divide-by-zero guard" "$OUT" "division by zero"
expect_contains "mock model echoes other input"    "$OUT" 'You said: "what is the weather"'
expect_contains "clean shutdown prints 'bye'"      "$OUT" "bye"

# =========================================================================
say ""
say "== 3. Context window: keeps last 5 turns, evicts the oldest =="

# 6 recorded turns, then ask for the history view.
OUT=$(printf 'msg1\nmsg2\nmsg3\nmsg4\nmsg5\nmsg6\nhistory\nexit\n' | "$BIN")

expect_absent   "oldest turn (msg1) evicted"      "$OUT" "you: msg1"
expect_contains "msg2 still in window"             "$OUT" "you: msg2"
expect_contains "newest turn (msg6) in window"     "$OUT" "you: msg6"
expect_contains "history reports 5 turns"          "$OUT" "last 5 turn(s)"

# 'history' and empty lines must NOT themselves consume a turn slot.
OUT=$(printf '\n\nonly real turn\nhistory\nexit\n' | "$BIN")
expect_contains "blank lines are not recorded"     "$OUT" "last 1 turn(s)"

# =========================================================================
say ""
say "== 4. Memory safety =="

SCRIPT='hello\ncalc 3 + 4\nmsg1\nmsg2\nmsg3\nmsg4\nmsg5\nmsg6\nhistory\nexit\n'

if command -v valgrind > /dev/null 2>&1; then
    if printf "$SCRIPT" | valgrind --quiet --error-exitcode=99 \
            --leak-check=full --errors-for-leak-kinds=all "$BIN" > /dev/null 2> vg.log; then
        pass "valgrind: no leaks, no errors"
    else
        fail "valgrind reported problems:"
        sed 's/^/        /' vg.log
    fi
else
    say "  (valgrind not found -- falling back to AddressSanitizer/UBSan)"
    if $CC -std=c11 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
            "$SRC" -o harness_asan 2> asan_build.log; then
        if printf "$SCRIPT" | ./harness_asan > /dev/null 2> asan.log; then
            pass "ASan/UBSan: no leaks, no errors"
        else
            fail "sanitizer reported problems:"
            sed 's/^/        /' asan.log
        fi
        rm -f harness_asan
    else
        fail "could not build sanitizer variant:"
        sed 's/^/        /' asan_build.log
    fi
fi

# =========================================================================
say ""
rm -f build.log vg.log asan.log asan_build.log
if [ "$FAILED" -eq 0 ]; then
    say "ALL TESTS PASSED"
    exit 0
else
    say "SOME TESTS FAILED"
    exit 1
fi
