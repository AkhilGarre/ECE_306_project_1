# Vibe Coding Log — ECE 309 Project 1 (LLM Mini-Harness)

This log records every prompt used to generate/modify code for this project,
verbatim, plus what the AI produced and what had to be corrected. Entries are
appended as work happens, not reconstructed afterward.

---

## Entry 0 — 2026-08-31 — Project scaffold

**Context (not a generation prompt):** Set up the repo skeleton before any
spec or code work began.

- created a proper `ece309_harness/` directory.
- Created empty placeholders: `harness.c`, `test.sh`, `README.md`,
  `vibe_coding_log.md` (this file).

No code generated yet. Next: work through the spec (core loop, context
management, tool-trigger design) before generating `harness.c`.

---

## Entry 1 — 2026-09-05 — Specification (SDD)

**Context (not a generation prompt):** Before asking the AI for any C, we
wrote the full technical contract in `SPEC.md`. Architectural rules fixed:

- **Language / constraints:** standard C11 only — `<stdio.h>`, `<stdlib.h>`,
  `<string.h>`, `<ctype.h>`. No POSIX-only calls (`strdup`, `getline`), no
  third-party libraries. Must build clean under
  `gcc -std=c11 -Wall -Wextra` *and* under the bare `gcc harness.c -o harness`.
- **State machine:** `PROMPT → ROUTE → RECORD → PROMPT`, with `exit` or EOF as
  the only shutdown triggers (so piped input ends cleanly). Written out as a
  diagram in `SPEC.md` §3.
- **Mock model (§4):** deterministic. Substring `hello` (case-insensitive) →
  fixed greeting; otherwise echo `You said: "<input>"`. No randomness so the
  test script is reproducible.
- **Tool layer (§5):** the *harness* — not the model — detects the tool call.
  Trigger = line begins with `calc `. Grammar `<number> <op> <number>`,
  ops `+ - * /`, `strtod` operands, explicit divide-by-zero and malformed-input
  errors. Result string is what gets stored as the assistant turn.
- **Context management (§6):** `Turn { char *user; char *assistant; }`,
  `history[5]` used as a ring buffer, `turns_seen % 5` selects the slot, the
  old occupant is `free`d before being overwritten. A `dup_str` helper
  (`malloc` + `memcpy`) replaces the non-standard `strdup`. `history_free()`
  walks all 5 slots on every exit path. A `history` command prints the window
  without consuming a turn.
- **Failure policy (§8):** allocation failure → message on `stderr`, exit 1.

## Entry 2 — 2026-09-05 — Generate `harness.c`

**Prompt given to the AI:**

> You are my junior C developer. Implement `harness.c` exactly to `SPEC.md`
> in this repo — single file, C11, standard headers only, no POSIX-only
> functions. Heavily comment it, and label the sections with the SPEC
> section numbers (§3 core loop, §4 mock model, §5 tool, §6 context
> management). It must compile with `gcc -std=c11 -Wall -Wextra -Werror`.
> Implement: the prompt/route/record loop; `exit` and EOF shutdown; the
> `calc ` tool trigger with a `strtod`-based `<number> <op> <number>` parser
> and divide-by-zero handling; the 5-turn ring-buffer history with a
> `dup_str` helper and a `history_free` that runs on every exit path; and a
> `history` command that views the window without recording a turn.

**AI produced:** the full `harness.c` (see git history). Structure matched the
spec — ring-buffer history, `dup_str`, `contains_ci` for the case-insensitive
match, `calculator()` returning a `malloc`'d result string, `drain_line()` to
handle over-long input lines.

**Corrections made after review:**

1. First draft assembled the echo response with `strcat` calls. Replaced with
   explicit `memcpy` into a `malloc(plen + ulen + slen + 1)` buffer so the
   size arithmetic is visible and there is no chance of a fortify /
   `-Wstringop-overflow` complaint.
2. Initialised `double result = 0.0;` in `calculator()` to keep
   `-Wmaybe-uninitialized` quiet even though every `switch` path already
   assigns or returns.
3. Fixed a stale header comment that listed `strpbrk` (unused); actual string
   calls are `strlen`, `strcmp`, `strncmp`, `strchr`, `memcpy`.

## Entry 3 — 2026-09-05 — Generate `test.sh`

**Prompt given to the AI:**

> Write `test.sh` (bash, no human interaction) that verifies `harness.c`
> against `SPEC.md` §9: (1) build with
> `gcc -std=c11 -Wall -Wextra -Werror -g` and fail on any warning, plus prove
> the bare `gcc harness.c -o harness` works; (2) pipe scripted sessions in and
> assert the greeting fires on `hello`, `calc 6 / 2` gives `= 3`, `calc 1 / 0`
> reports division by zero, and the echo path works; (3) send 6 turns then
> `history` and assert the oldest (`msg1`) was evicted while `msg6` survived,
> and that blank lines / `history` don't consume a turn slot; (4) a memory
> check — `valgrind --leak-check=full --error-exitcode=99` if present, else a
> `-fsanitize=address,undefined` rebuild. Print `ALL TESTS PASSED` / exit 0 on
> success, `SOME TESTS FAILED` / exit 1 otherwise.

**AI produced:** `test.sh` with `expect_contains` / `expect_absent` helpers
using shell `case` globbing (no external `grep` needed), the four test
sections, and log cleanup.

**Not yet run:** no C compiler is installed on the dev machine (no gcc, no WSL).
Compilation and `bash test.sh` still need to be run in a POSIX environment
(WSL / Linux lab machine / macOS). See README "Building".
