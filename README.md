# ECE 309 Project 1 — LLM Mini-Harness in C

A minimal **agent harness**: the layer that sits between a human at a terminal
and a (mock) language model. It was built with *specification-driven vibe
coding* — the spec ([`SPEC.md`](SPEC.md)) was written first, then an AI
assistant generated the C from it. Every prompt and correction is recorded in
[`vibe_coding_log.md`](vibe_coding_log.md).

## What it does

| Responsibility        | Where                                             |
|-----------------------|--------------------------------------------------|
| **Core loop**         | `main()` — prompt → route → record → repeat; `exit` or EOF shuts down |
| **Mock model**        | `mock_model()` — deterministic: `hello` → greeting, else echoes input |
| **Tool execution**    | `calculator()` — the harness intercepts `calc <a> <op> <b>` and does the arithmetic itself (`+ - * /`, divide-by-zero guarded) |
| **Context management** | `history[5]` ring buffer of `(user, assistant)` turns, heap-allocated via `dup_str`, freed on every exit path; `history` command views it |

## Building

Standard C11, POSIX environment. No external libraries.

```sh
gcc -std=c11 -Wall -Wextra harness.c -o harness
# or the bare grader command:
gcc harness.c -o harness
```

> On Windows, build inside **WSL** (`wsl --install`, then `sudo apt install gcc`)
> or a Linux/macOS machine — there is no compiler on plain Windows.

## Running

```
$ ./harness
LLM mini-harness. Commands: 'calc <a> <op> <b>', 'history', 'exit'.
you> hello there
bot> Hello! I am a mock model. How can I help you today?
you> calc 6 / 2
bot> [tool:calculator] 6 / 2 = 3
you> what's the capital of France
bot> You said: "what's the capital of France"
you> history
bot> conversation history (last 3 turn(s), oldest first):
  [1] you: hello there
      bot: Hello! I am a mock model. How can I help you today?
  [2] you: calc 6 / 2
      bot: [tool:calculator] 6 / 2 = 3
  [3] you: what's the capital of France
      bot: You said: "what's the capital of France"
you> exit
bye
```

## Testing

```sh
bash test.sh
```

`test.sh` builds with warnings-as-errors, pipes scripted sessions through the
binary to check the model / tool / eviction behaviour, and runs a memory check
(`valgrind` if available, otherwise an AddressSanitizer + UBSan build). It
prints `ALL TESTS PASSED` and exits 0 on success.

## Files

| File                  | Purpose                                       |
|-----------------------|-----------------------------------------------|
| `SPEC.md`             | The technical specification (written first)   |
| `harness.c`           | The single-file C implementation             |
| `test.sh`             | AI-generated automated test / leak check     |
| `vibe_coding_log.md`  | Every prompt, AI response summary, correction |
