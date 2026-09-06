/* =========================================================================
 * harness.c  --  LLM Mini-Harness (ECE 309 Project 1)
 *
 * A minimal "agent harness": the glue between a human at a terminal and a
 * (mock) language model.  It demonstrates the three jobs a real harness does:
 *
 *   1. Core loop      - read a line, route it, print a response  (SPEC sec. 3)
 *   2. Context mgmt    - keep the last 5 turns on the heap, leak-free (sec. 6)
 *   3. Tool execution  - hand arithmetic to a real C function     (sec. 5)
 *
 * Standard C only (C11).  Build:
 *     gcc -std=c11 -Wall -Wextra harness.c -o harness
 * or simply:
 *     gcc harness.c -o harness
 *
 * See SPEC.md for the full specification this file implements.
 * ========================================================================= */

#include <stdio.h>   /* printf, fgets, fputs, stdin, stdout, stderr */
#include <stdlib.h>  /* malloc, free, strtod, exit, EXIT_FAILURE    */
#include <string.h>  /* strlen, strcmp, strncmp, strchr, memcpy     */
#include <ctype.h>   /* tolower, isspace                            */

/* ---- compile-time configuration ---------------------------------------- */

#define MAX_LINE  1024   /* size of the fixed stdin read buffer          */
#define MAX_TURNS 5      /* how many (user, assistant) turns we remember */

/* =========================================================================
 * Small string helper
 *
 * strdup() is POSIX, not standard C, so we roll our own.  Returns a fresh
 * malloc'd copy of s, or NULL if allocation fails.
 * ========================================================================= */
static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;      /* +1 for the terminating '\0' */
    char *copy = malloc(n);
    if (copy != NULL) {
        memcpy(copy, s, n);
    }
    return copy;
}

/* Print an allocation-failure message and bail out.  Called only when the
 * process genuinely cannot continue (out of memory). */
static void die_oom(void)
{
    fputs("harness: out of memory\n", stderr);
    exit(EXIT_FAILURE);
}

/* =========================================================================
 * SECTION 6 -- Context management (the "context window")
 *
 * history[] is used as a ring buffer.  turns_seen counts every turn ever
 * recorded; the live slot for the next turn is (turns_seen % MAX_TURNS).
 * Once we have wrapped, that slot still holds the oldest turn, so we free
 * it before overwriting.  Net effect: at most the last MAX_TURNS turns are
 * kept, and every malloc has a matching free.
 * ========================================================================= */

typedef struct {
    char *user;        /* malloc'd copy of the user's line */
    char *assistant;   /* malloc'd copy of the response    */
} Turn;

static Turn   history[MAX_TURNS];   /* zero-initialised: all pointers NULL */
static size_t turns_seen = 0;       /* total turns recorded this run       */

/* Record one (user, assistant) exchange, evicting the oldest if the buffer
 * is full. */
static void history_record(const char *user, const char *assistant)
{
    size_t slot = turns_seen % MAX_TURNS;

    /* If we have wrapped around, this slot holds the oldest turn. Free it. */
    if (turns_seen >= MAX_TURNS) {
        free(history[slot].user);
        free(history[slot].assistant);
    }

    history[slot].user      = dup_str(user);
    history[slot].assistant = dup_str(assistant);
    if (history[slot].user == NULL || history[slot].assistant == NULL) {
        die_oom();
    }

    turns_seen++;
}

/* Print the remembered turns, oldest first.  Does not touch the model or
 * record a new turn -- it is a pure view onto the context. */
static void history_print(void)
{
    /* How many turns are actually stored right now. */
    size_t stored = (turns_seen < MAX_TURNS) ? turns_seen : (size_t)MAX_TURNS;

    /* Index of the oldest stored turn within the ring. */
    size_t start = (turns_seen < MAX_TURNS) ? 0 : (turns_seen % MAX_TURNS);

    if (stored == 0) {
        printf("bot> (history is empty)\n");
        return;
    }

    printf("bot> conversation history (last %zu turn(s), oldest first):\n",
           stored);
    for (size_t i = 0; i < stored; i++) {
        size_t slot = (start + i) % MAX_TURNS;
        printf("  [%zu] you: %s\n", i + 1, history[slot].user);
        printf("      bot: %s\n", history[slot].assistant);
    }
}

/* Free every string still held in the ring buffer.  Safe to call once, on
 * any exit path.  free(NULL) is a no-op, so empty slots are fine. */
static void history_free(void)
{
    for (int i = 0; i < MAX_TURNS; i++) {
        free(history[i].user);
        free(history[i].assistant);
        history[i].user      = NULL;
        history[i].assistant = NULL;
    }
}

/* =========================================================================
 * SECTION 4 -- The mock model
 *
 * Deterministic stand-in for an LLM so the harness is fully testable with
 * no network.  Returns a malloc'd string the caller must free.
 * ========================================================================= */

/* Case-insensitive substring search (strcasestr is not standard C). */
static int contains_ci(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0) {
        return 1;
    }
    for (; *haystack != '\0'; haystack++) {
        size_t i = 0;
        while (i < nlen &&
               tolower((unsigned char)haystack[i]) ==
               tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) {
            return 1;
        }
    }
    return 0;
}

static char *mock_model(const char *user)
{
    if (contains_ci(user, "hello")) {
        return dup_str("Hello! I am a mock model. How can I help you today?");
    }

    /* Default behaviour: echo the user's text back inside quotes.
     * Response is:  You said: "<user>"  -- allocate exactly enough and
     * assemble it with memcpy so the size math is explicit. */
    {
        const char  prefix[] = "You said: \"";
        const char  suffix[] = "\"";
        size_t plen = sizeof prefix - 1;   /* drop the '\0' */
        size_t ulen = strlen(user);
        size_t slen = sizeof suffix - 1;
        char *out = malloc(plen + ulen + slen + 1);
        if (out == NULL) {
            die_oom();
        }
        memcpy(out,               prefix, plen);
        memcpy(out + plen,        user,   ulen);
        memcpy(out + plen + ulen, suffix, slen + 1);   /* +1 copies '\0' */
        return out;
    }
}

/* =========================================================================
 * SECTION 5 -- Tool layer: a real calculator
 *
 * The harness detects the "calc " prefix and routes here instead of to the
 * model, because arithmetic is exactly what an LLM should NOT be trusted to
 * do.  Grammar: <number> <op> <number>, whitespace separated.
 * Returns a malloc'd string the caller must free.
 * ========================================================================= */

static char *calc_error(void)
{
    return dup_str("[tool:calculator] error: expected '<number> <op> <number>'");
}

static char *calculator(const char *expr)
{
    double a, b, result = 0.0;
    char op;
    char *endp;
    const char *p = expr;

    /* --- operand A --- */
    a = strtod(p, &endp);
    if (endp == p) {                 /* no number parsed */
        return calc_error();
    }
    p = endp;

    /* --- skip spaces, read the operator --- */
    while (isspace((unsigned char)*p)) {
        p++;
    }
    op = *p;
    if (op != '+' && op != '-' && op != '*' && op != '/') {
        return calc_error();
    }
    p++;

    /* --- operand B --- */
    b = strtod(p, &endp);
    if (endp == p) {                 /* no second number */
        return calc_error();
    }
    p = endp;

    /* --- nothing but trailing whitespace may remain --- */
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '\0') {
        return calc_error();
    }

    /* --- evaluate --- */
    switch (op) {
        case '+': result = a + b; break;
        case '-': result = a - b; break;
        case '*': result = a * b; break;
        case '/':
            if (b == 0.0) {
                return dup_str("[tool:calculator] error: division by zero");
            }
            result = a / b;
            break;
        default:  /* unreachable: op already validated */
            return calc_error();
    }

    /* --- format the answer --- */
    {
        char buf[128];
        int n = snprintf(buf, sizeof buf,
                         "[tool:calculator] %g %c %g = %g", a, op, b, result);
        if (n < 0 || (size_t)n >= sizeof buf) {
            return dup_str("[tool:calculator] error: result too large to format");
        }
        return dup_str(buf);
    }
}

/* =========================================================================
 * Input helpers
 * ========================================================================= */

/* Remove a single trailing '\n' if present. */
static void strip_newline(char *s)
{
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] == '\n') {
        s[n - 1] = '\0';
    }
}

/* If fgets did not capture a newline, the line was longer than the buffer;
 * drain the rest so the leftover is not read as a second turn. */
static void drain_line(const char *s)
{
    if (strchr(s, '\n') == NULL) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {
            /* discard */
        }
    }
}

/* =========================================================================
 * SECTION 3 -- The core loop
 * ========================================================================= */

int main(void)
{
    char line[MAX_LINE];

    printf("LLM mini-harness. Commands: 'calc <a> <op> <b>', 'history', "
           "'exit'.\n");

    for (;;) {
        printf("you> ");
        fflush(stdout);                 /* make sure the prompt shows first */

        /* ---- read one line; NULL means EOF (Ctrl-D or end of pipe) ---- */
        if (fgets(line, sizeof line, stdin) == NULL) {
            printf("\n");
            break;
        }
        drain_line(line);
        strip_newline(line);

        /* ---- routing (SPEC sec. 3.1 / 3.2) ---- */

        if (strcmp(line, "exit") == 0) {
            break;                          /* clean shutdown trigger */
        }
        if (line[0] == '\0') {
            continue;                       /* empty line: nothing stored */
        }
        if (strcmp(line, "history") == 0) {
            history_print();                /* view context, no new turn */
            continue;
        }

        /* ---- produce a response: tool path or model path ---- */
        char *response;
        if (strncmp(line, "calc ", 5) == 0 && line[5] != '\0') {
            response = calculator(line + 5);   /* SECTION 5: tool */
        } else {
            response = mock_model(line);       /* SECTION 4: mock model */
        }
        if (response == NULL) {
            die_oom();
        }

        /* ---- record the turn, then show the response ---- */
        history_record(line, response);
        printf("bot> %s\n", response);

        free(response);
    }

    /* ---- SECTION 6: tear down the context window, leak-free ---- */
    history_free();
    printf("bye\n");
    return 0;
}
