# SPEC.md — LLM Mini-Harness (ECE 309 Project 1)

Specification-Driven Development document. This is the contract the C code must
satisfy. Written **before** `harness.c` was generated. Every prompt fed to the AI
references the section numbers below.

---

## 1. Purpose

A single-file C program (`harness.c`) that acts as a minimal *agent harness*: it
sits between a human at a terminal and a **mock** language model, and it owns
three responsibilities a real harness owns:

1. **Core loop** — read user input, route it, print a response.
2. **Context management** — keep a bounded, dynamically-allocated conversation
   history (last 5 turns) with no leaks.
3. **Tool execution** — recognise a request the "model" cannot do reliably
   (arithmetic) and dispatch it to a real C function instead.

No network, no LLM API. The "model" is a deterministic function so the program
is fully testable from a script.

---

## 2. Hard constraints

- Standard C only: `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<ctype.h>`.
  No POSIX-only calls (no `strdup`, no `getline`), no third-party libraries.
- Must compile clean with: `gcc -std=c11 -Wall -Wextra harness.c -o harness`
- Must also compile with the bare grader command: `gcc harness.c -o harness`
- Single translation unit. No headers of our own.
- All heap allocations are checked and freed before `main` returns.
- Portable to any POSIX environment (Linux/WSL/macOS).

---

## 3. State machine

```
        start
          |
          v
   +-------------+     input == "exit"  or EOF
   |   PROMPT    |----------------------------------> SHUTDOWN
   |  print "you> "                                       |
   |  fgets line |                                        v
   +-------------+                                  free all history
          | line read                              print "bye"
          v                                        return 0
   +-------------+
   |   ROUTE     |
   +-------------+
     |    |    |
     |    |    +--- empty line ------------> back to PROMPT (nothing stored)
     |    |
     |    +-------- starts with "calc " ---> TOOL: calculator()
     |                                       response = tool result string
     |
     +------------- anything else ---------> MODEL: mock_model()
                                             response = model string
          |
          v
   +-------------+
   | RECORD TURN |  store {user line, response} in history (evict oldest if >5)
   +-------------+
          |
          v
   print response  --> back to PROMPT
```

### 3.1 Shutdown triggers
- The line (after trimming the trailing newline) equals `exit`.
- End-of-file on stdin (`fgets` returns `NULL`) — so piped input terminates cleanly.

### 3.2 Input handling
- Fixed read buffer of `MAX_LINE = 1024` bytes via `fgets`.
- Strip a single trailing `\n`.
- If the line was longer than the buffer, drain the rest of the line from stdin
  so the leftover is not parsed as a new turn.

---

## 4. Mock model — `mock_model(const char *user)`

Deterministic. Returns a newly `malloc`'d string the caller must free.

| Condition (checked in order)                     | Response                                        |
|-------------------------------------------------|-------------------------------------------------|
| `user` contains the substring `hello` (case-insensitive) | `"Hello! I am a mock model. How can I help you today?"` |
| otherwise                                       | `"You said: \"<user>\""` (echo)                 |

---

## 5. Tool layer

### 5.1 Trigger
The harness — not the model — detects the tool call. Trigger: the line begins
with the literal prefix `calc ` (case-sensitive, followed by at least one more
character). This is a deterministic, script-friendly trigger.

### 5.2 `calculator(const char *expr)`
- `expr` is everything after the `calc ` prefix.
- Grammar: `<number> <op> <number>` — whitespace-separated, exactly three tokens.
  - `<number>`: parsed with `strtod`. Integers and decimals allowed; leading `-` allowed.
  - `<op>`: one of `+  -  *  /`
- Returns a newly `malloc`'d string:
  - success: `"[tool:calculator] <a> <op> <b> = <result>"` — result printed with
    `%g` so `6 / 2` shows `3`, not `3.000000`.
  - division by zero: `"[tool:calculator] error: division by zero"`
  - malformed input: `"[tool:calculator] error: expected '<number> <op> <number>'"`
- The tool result string is what gets recorded as the assistant turn in history.

---

## 6. Context management

```c
#define MAX_TURNS 5

typedef struct {
    char *user;        /* malloc'd copy of the user's line       */
    char *assistant;   /* malloc'd copy of the response          */
} Turn;
```

- Storage: `static Turn history[MAX_TURNS];` used as a **ring buffer**.
- `static size_t turns_seen;` counts every recorded turn for the life of the run.
- Record: slot = `turns_seen % MAX_TURNS`. If that slot is occupied (we have
  wrapped), `free` both strings in it first, then store the new copies. Increment
  `turns_seen`.
- Copies are made by a local `dup_str(const char *)` helper (`malloc` + `memcpy`),
  because `strdup` is not standard C. `dup_str` returns `NULL` only on allocation
  failure; the program prints an error and exits non-zero in that case.
- `history` command: if the user line equals `history`, the harness prints the
  stored turns oldest→newest (at most 5) and does **not** call the model, does
  **not** record a new turn. Format per turn:
  ```
  [n] you: <user>
      bot: <assistant>
  ```
- Cleanup: a `history_free(void)` function walks all `MAX_TURNS` slots and frees
  any non-NULL strings. Called once, just before `main` returns, on every exit
  path (normal `exit`, EOF).

---

## 7. Output format

Startup banner (once):
```
LLM mini-harness. Commands: 'calc <a> <op> <b>', 'history', 'exit'.
```
Per turn: `bot> <response>\n`
Prompt: `you> ` (no newline)
Shutdown: `bye\n`

---

## 8. Exit codes
- `0` — clean shutdown (`exit` or EOF).
- `1` — unrecoverable allocation failure (message on `stderr`).

---

## 9. Test surface (drives `test.sh`)

`test.sh` must, without human interaction:
1. Build with `gcc -std=c11 -Wall -Wextra -g harness.c -o harness` and fail on
   any warning/error.
2. Feed a scripted session on stdin and assert:
   - a line containing `hello` → greeting text appears.
   - `calc 6 / 2` → output contains `= 3`.
   - `calc 1 / 0` → output contains `division by zero`.
   - **eviction:** send `msg1`..`msg6` (6 turns), then `history`; assert `msg6`
     is present and `msg1` is absent (only 5 kept).
   - `exit` → program terminates, final line is `bye`.
3. Memory check, best available:
   - if `valgrind` present: run under
     `valgrind --error-exitcode=99 --leak-check=full`; fail on leak/error.
   - else: rebuild with `-fsanitize=address,undefined`, re-run the script, fail
     on any sanitizer report.
4. Print `ALL TESTS PASSED` and exit 0, or print the failing check and exit 1.
