/**
 * @file calc.c
 * @brief Interactive integer calculator with configurable input/output bases.
 *
 * Supports +, -, *, /, % operations on 64-bit signed integers.
 * Input and output bases can be set to 2, 8, 10, or 16 via the
 * @c ibase and @c obase commands.
 */

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 256
#define CALC_VERSION "1.1.0"

static int ibase = 10;      /**< Current input base (2, 8, 10, or 16). */
static int obase = 10;      /**< Current output base (2, 8, 10, or 16). */
static int ibase_float = 0; /**< Non-zero when ibase is "10f" (floating-point input). */
static int obase_float = 0; /**< Non-zero when obase is "10f" (floating-point output). */

/**
 * @brief Check whether @p b is an accepted number base.
 * @param b Base to validate.
 * @return Non-zero if @p b is 2, 8, 10, or 16; zero otherwise.
 */
static int valid_base(int b) {
    return b == 2 || b == 8 || b == 10 || b == 16;
}

/* Returns non-zero if s is "10f" or "10F". */
static int is_float_base(const char *s) {
    return s[0] == '1' && s[1] == '0' &&
           (s[2] == 'f' || s[2] == 'F') && s[3] == '\0';
}

/**
 * @brief Print @p val to stdout in the current output base (@c obase).
 *
 * Handles negative values and the special case of zero.
 * Digits above 9 are rendered as uppercase letters (A–F).
 * @param val Value to print.
 */
static void print_result(int64_t val) {
    if (obase == 10) {
        printf("%" PRId64 "\n", val);
        return;
    }

    if (val == 0) {
        if (obase == 2)
            printf("00000000\n");
        else
            printf("0\n");
        return;
    }

    char buf[128];
    int neg = 0;
    uint64_t uval;

    if (val < 0) {
        neg = 1;
        /* Cast before negating: unsigned wrap is defined, signed negation of INT64_MIN is UB */
        uval = ~(uint64_t)val + (uint64_t)1;
    } else {
        uval = (uint64_t)val;
    }

    int i = 0;
    while (uval > 0) {
        int digit = (int)(uval % (uint64_t)obase);
        buf[i++] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        uval /= (uint64_t)obase;
    }

    if (obase == 2) {
        int target = (i <= 8) ? 8 : (i <= 16) ? 16
                                : (i <= 32)   ? 32
                                              : 64;
        while (i < target)
            buf[i++] = '0';
    }

    if (neg)
        buf[i++] = '-';
    buf[i] = 0;

    /* reverse */
    for (int l = 0, r = i - 1; l < r; l++, r--) {
        char tmp = buf[l];
        buf[l] = buf[r];
        buf[r] = tmp;
    }
    printf("%s\n", buf);
}

/**
 * @brief Parse a number token using the current input base (@c ibase).
 *
 * Accepts an optional leading @c - sign. Rejects digits that are
 * out of range for @c ibase.
 * @param s   Null-terminated token to parse.
 * @param out Receives the parsed value on success.
 * @return    0 on success, -1 on parse error or invalid digit.
 */
static int parse_number(const char *s, int64_t *out) {
    if (*s == 0)
        return -1;
    char *end;
    errno = 0;
    *out = (int64_t)strtoll(s, &end, ibase);
    if (errno != 0 || *end != 0)
        return -1;
    /* validate digits are legal for the base */
    for (const char *p = s + (*s == '-' ? 1 : 0); *p; p++) {
        int d;
        if (*p >= '0' && *p <= '9')
            d = *p - '0';
        else if (*p >= 'a' && *p <= 'f')
            d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F')
            d = *p - 'A' + 10;
        else
            return -1;
        if (d >= ibase)
            return -1;
    }
    return 0;
}

/* Parse a decimal floating-point token into *out. Returns 0 on success, -1 on error. */
static int parse_float(const char *s, double *out) {
    if (*s == 0)
        return -1;
    char *end;
    errno = 0;
    *out = strtod(s, &end);
    if (errno != 0 || *end != 0)
        return -1;
    return 0;
}

/**
 * @brief Trim leading and trailing whitespace from @p s in-place.
 * @param s Mutable, null-terminated string to trim.
 * @return  Pointer to the first non-whitespace character within @p s.
 */
static char *trim(char *s) {
    while (isspace((unsigned char)*s))
        s++;
    if (*s == 0)
        return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        *end-- = 0;
    return s;
}

/**
 * @brief Locate the primary operator in an infix expression string.
 *
 * Precedence rules (lowest wins): @c + / @c - are searched first
 * (right-to-left), then @c * / @c / / @c % , then @c ^ if no
 * lower-precedence operator is found. A leading sign character at
 * position 0 is never treated as an operator.
 * @param s      Null-terminated expression string.
 * @param op     Receives the operator character on success.
 * @param op_pos Receives the byte offset of the operator on success.
 * @return       0 on success, -1 if no operator is found.
 */
static int find_operator(const char *s, char *op, int *op_pos) {
    /* Scan right-to-left for + or -, then for * / % if not found */
    int pos = -1;
    char found = 0;

    for (int i = (int)strlen(s) - 1; i >= 0; i--) {
        /* skip sign at position 0 */
        if (i == 0 && (s[i] == '+' || s[i] == '-'))
            break;
        if (s[i] == '+' || s[i] == '-') {
            /* in float mode skip the sign character inside exponents (e.g. 1.5e+2) */
            if (ibase_float && i > 0 && (s[i - 1] == 'e' || s[i - 1] == 'E'))
                continue;
            pos = i;
            found = s[i];
            break;
        }
    }

    if (found == 0) {
        for (int i = (int)strlen(s) - 1; i >= 0; i--) {
            if (s[i] == '*' || s[i] == '/' || s[i] == '%') {
                pos = i;
                found = s[i];
                break;
            }
        }
    }

    if (found == 0) {
        for (int i = (int)strlen(s) - 1; i >= 0; i--) {
            if (s[i] == '^') {
                pos = i;
                found = s[i];
                break;
            }
        }
    }

    if (found == 0)
        return -1;
    *op = found;
    *op_pos = pos;
    return 0;
}

/**
 * @brief Parse and evaluate a single expression line.
 *
 * If no operator is found the input is treated as a bare number and
 * printed in @c obase (useful for base conversion). Division and
 * modulo by zero, and signed overflow on division, are reported as
 * errors to stderr.
 * @param line Null-terminated input line (whitespace already trimmed).
 */
static void handle_expression(const char *line) {
    char buf[LINE_MAX_LEN];
    strncpy(buf, line, LINE_MAX_LEN - 1);
    buf[LINE_MAX_LEN - 1] = 0;

    /* Normalize ** to ^ */
    for (int i = 0; buf[i] && buf[i + 1]; i++) {
        if (buf[i] == '*' && buf[i + 1] == '*') {
            buf[i] = '^';
            memmove(buf + i + 1, buf + i + 2, strlen(buf + i + 2) + 1);
        }
    }

    char op = 0;
    int op_pos = -1;

    if (find_operator(buf, &op, &op_pos) != 0) {
        /* No operator — bare number (base conversion / echo) */
        if (ibase_float) {
            double val;
            if (parse_float(trim(buf), &val) == 0) {
                if (obase_float)
                    printf("%g\n", val);
                else
                    print_result((int64_t)val);
            } else {
                fprintf(stderr, "error: unrecognized input: %s\n", line);
            }
        } else {
            int64_t val;
            if (parse_number(trim(buf), &val) == 0) {
                if (obase_float)
                    printf("%g\n", (double)val);
                else
                    print_result(val);
            } else {
                fprintf(stderr, "error: unrecognized input: %s\n", line);
            }
        }
        return;
    }

    char lhs_s[LINE_MAX_LEN], rhs_s[LINE_MAX_LEN];
    strncpy(lhs_s, buf, (size_t)op_pos);
    lhs_s[op_pos] = 0;
    strncpy(rhs_s, buf + op_pos + 1, LINE_MAX_LEN - 1);
    rhs_s[LINE_MAX_LEN - 1] = 0;

    char *lhs_t = trim(lhs_s);
    char *rhs_t = trim(rhs_s);

    if (ibase_float) {
        /* --- floating-point path --- */
        double lhs, rhs, result;
        if (parse_float(lhs_t, &lhs) != 0) {
            fprintf(stderr, "error: invalid left operand: %s\n", lhs_t);
            return;
        }
        if (parse_float(rhs_t, &rhs) != 0) {
            fprintf(stderr, "error: invalid right operand: %s\n", rhs_t);
            return;
        }
        switch (op) {
        case '+':
            result = lhs + rhs;
            break;
        case '-':
            result = lhs - rhs;
            break;
        case '*':
            result = lhs * rhs;
            break;
        case '/':
            if (rhs == 0.0) {
                fprintf(stderr, "error: division by zero\n");
                return;
            }
            result = lhs / rhs;
            break;
        case '%':
            if (rhs == 0.0) {
                fprintf(stderr, "error: modulo by zero\n");
                return;
            }
            result = fmod(lhs, rhs);
            break;
        case '^':
            result = pow(lhs, rhs);
            break;
        default:
            fprintf(stderr, "error: unknown operator\n");
            return;
        }
        if (obase_float)
            printf("%g\n", result);
        else
            print_result((int64_t)result);
        return;
    }

    /* --- integer path --- */
    int64_t lhs, rhs;
    if (parse_number(lhs_t, &lhs) != 0) {
        fprintf(stderr, "error: invalid left operand: %s\n", lhs_t);
        return;
    }
    if (parse_number(rhs_t, &rhs) != 0) {
        fprintf(stderr, "error: invalid right operand: %s\n", rhs_t);
        return;
    }

    int64_t result;
    switch (op) {
    case '+':
        result = lhs + rhs;
        break;
    case '-':
        result = lhs - rhs;
        break;
    case '*':
        result = lhs * rhs;
        break;
    case '/':
        if (rhs == 0) {
            fprintf(stderr, "error: division by zero\n");
            return;
        }
        if (lhs == INT64_MIN && rhs == -1) {
            fprintf(stderr, "error: overflow\n");
            return;
        }
        result = lhs / rhs;
        break;
    case '%':
        if (rhs == 0) {
            fprintf(stderr, "error: modulo by zero\n");
            return;
        }
        if (lhs == INT64_MIN && rhs == -1) {
            result = 0;
            break;
        }
        result = lhs % rhs;
        break;
    case '^':
        if (rhs < 0) {
            fprintf(stderr, "error: negative exponent\n");
            return;
        }
        result = 1;
        {
            int64_t base = lhs;
            int64_t exp = rhs;
            while (exp > 0) {
                if (exp & 1)
                    result *= base;
                exp >>= 1;
                if (exp > 0)
                    base *= base;
            }
        }
        break;
    default:
        fprintf(stderr, "error: unknown operator\n");
        return;
    }
    if (obase_float)
        printf("%g\n", (double)result);
    else
        print_result(result);
}

/**
 * @brief Entry point — runs the interactive calculator REPL.
 *
 * Reads one line at a time from stdin. Special commands:
 *   - @c exit / @c quit — terminate the session.
 *   - @c ibase @c \<n\> — set the input base (2, 8, 10, 16).
 *   - @c obase @c \<n\> — set the output base (2, 8, 10, 16).
 *   - Any other input is passed to handle_expression().
 * @return 0 on normal exit.
 */
int main(void) {
    char line[LINE_MAX_LEN];
    printf("calc %s — type 'exit' or 'quit' to leave\n", CALC_VERSION);

    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
            break; /* EOF */

        char *s = trim(line);
        if (*s == 0)
            continue;

        if (strcmp(s, "exit") == 0 || strcmp(s, "quit") == 0)
            break;

        /* ibase <n> */
        if (strncmp(s, "ibase", 5) == 0 && (s[5] == ' ' || s[5] == '\t')) {
            char *arg = trim(s + 5);
            if (is_float_base(arg)) {
                ibase = 10;
                ibase_float = 1;
            } else {
                int b = atoi(arg);
                if (!valid_base(b)) {
                    fprintf(stderr, "error: ibase must be 2, 8, 10, 10f or 16\n");
                } else {
                    ibase = b;
                    ibase_float = 0;
                }
            }
            continue;
        }

        /* obase <n> */
        if (strncmp(s, "obase", 5) == 0 && (s[5] == ' ' || s[5] == '\t')) {
            char *arg = trim(s + 5);
            if (is_float_base(arg)) {
                obase = 10;
                obase_float = 1;
            } else {
                int b = atoi(arg);
                if (!valid_base(b)) {
                    fprintf(stderr, "error: obase must be 2, 8, 10, 10f or 16\n");
                } else {
                    obase = b;
                    obase_float = 0;
                }
            }
            continue;
        }

        handle_expression(s);
    }

    return 0;
}
