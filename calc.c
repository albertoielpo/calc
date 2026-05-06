/**
 * @file calc.c
 * @brief Interactive calculator with configurable input/output bases.
 *
 * Supports +, -, *, /, %, ^ operations with full operator precedence and
 * parentheses on 64-bit signed integers or IEEE-754 doubles (when ibase/obase
 * are set to @c 10f). Input and output bases can be set to 2, 8, 10, or 16
 * via the @c ibase and @c obase commands. The special token @c res always
 * holds the last computed result.
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
#define CALC_VERSION "1.5.0"
#define CLEAR_SCREEN "\033[2J\033[H"

static int ibase = 10;      /**< Current input base (2, 8, 10, or 16). */
static int obase = 10;      /**< Current output base (2, 8, 10, or 16). */
static int ibase_float = 0; /**< Non-zero when ibase is "10f" (floating-point input). */
static int obase_float = 0; /**< Non-zero when obase is "10f" (floating-point output). */

static int64_t last_result = 0;    /**< Last computed integer result (accessible as "res"). */
static double last_result_d = 0.0; /**< Last computed floating-point result (accessible as "res"). */

/**
 * @brief Check whether @p b is an accepted number base.
 * @param b Base to validate.
 * @return Non-zero if @p b is 2, 8, 10, or 16; zero otherwise.
 */
static int valid_base(int b) {
    return b == 2 || b == 8 || b == 10 || b == 16;
}

/**
 * @brief Check whether @p s represents the floating-point base specifier.
 * @param s Null-terminated string to check.
 * @return Non-zero if @p s is "10f" or "10F"; zero otherwise.
 */
static int is_float_base(const char *s) {
    return s[0] == '1' && s[1] == '0' &&
           (s[2] == 'f' || s[2] == 'F') && s[3] == 0;
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
        if (obase == 2) {
            printf("00000000\n");
        } else {
            printf("0\n");
        }
        return;
    }

    char buf[128];
    int neg = 0;
    uint64_t uval;

    if (obase == 2 && val < 0) {
        /* 2's complement: print the raw bit pattern at the minimum standard width */
        uval = (uint64_t)val;
        int target;
        if (val >= -128)
            target = 8;
        else if (val >= -32768)
            target = 16;
        else if (val >= INT32_MIN)
            target = 32;
        else
            target = 64;
        for (int j = 0; j < target; j++)
            buf[j] = (char)('0' + ((uval >> (target - 1 - j)) & 1));
        buf[target] = 0;
        printf("%s\n", buf);
        return;
    }

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
 * @brief Trim leading and trailing whitespace from @p s in-place.
 * @param s Mutable, null-terminated string to trim.
 * @return  Pointer to the first non-whitespace character within @p s.
 */
static char *trim(char *s) {
    while (isspace((uint8_t)*s))
        s++;
    if (*s == 0)
        return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((uint8_t)*end))
        *end-- = 0;
    return s;
}

/* ─── expression parser ──────────────────────────────────────────────────────
 *
 * Grammar (integer and floating-point modes share the same structure):
 *   expr    = term   { ('+' | '-') term   }
 *   term    = power  { ('*' | '/' | '%') power }
 *   power   = unary  [ '^' power ]          (right-associative)
 *   unary   = '-' unary | '+' unary | primary
 *   primary = NUMBER | 'res' | '(' expr ')'
 *
 * '**' is accepted as a synonym for '^'.
 */

#define T_NUM 0   /**< Numeric literal token. */
#define T_PLUS 1  /**< Addition operator @c +. */
#define T_MINUS 2 /**< Subtraction / unary-minus operator @c -. */
#define T_STAR 3  /**< Multiplication operator @c *. */
#define T_SLASH 4 /**< Division operator @c /. */
#define T_PCT 5   /**< Modulo operator @c %. */
#define T_CARET 6 /**< Exponentiation operator @c ^ (also accepted as @c **). */
#define T_LP 7    /**< Left parenthesis @c (. */
#define T_RP 8    /**< Right parenthesis @c ). */
#define T_END 9   /**< End-of-input sentinel. */
#define T_ERR 10  /**< Error sentinel set when the lexer cannot match a token. */

/**
 * @brief Parser state for the recursive-descent expression evaluator.
 */
typedef struct {
    const char *cur; /**< Current scan position in the input string. */
    int tok;         /**< Lookahead token type (one of the @c T_* constants). */
    int64_t ival;    /**< Numeric value of the last token in integer mode. */
    double dval;     /**< Numeric value of the last token in floating-point mode. */
    int err;         /**< Non-zero after any lexer or parser error. */
    char errmsg[80]; /**< Human-readable description of the first error encountered. */
} Parser;

/**
 * @brief Advance the parser to the next token.
 *
 * Skips whitespace, then reads one token from @p px->cur, setting
 * @p px->tok (and @p px->ival / @p px->dval for @c T_NUM). Sets
 * @p px->err and @p px->errmsg on an unrecognised token.
 * @param px Parser state to advance.
 */
static void px_advance(Parser *px) {
    while (isspace((uint8_t)*px->cur))
        px->cur++;

    switch (*px->cur) {
    case 0:
        px->tok = T_END;
        return;
    case '+':
        px->tok = T_PLUS;
        px->cur++;
        return;
    case '-':
        px->tok = T_MINUS;
        px->cur++;
        return;
    case '(':
        px->tok = T_LP;
        px->cur++;
        return;
    case ')':
        px->tok = T_RP;
        px->cur++;
        return;
    case '%':
        px->tok = T_PCT;
        px->cur++;
        return;
    case '^':
        px->tok = T_CARET;
        px->cur++;
        return;
    case '/':
        px->tok = T_SLASH;
        px->cur++;
        return;
    case '*':
        if (px->cur[1] == '*') {
            px->tok = T_CARET;
            px->cur += 2;
        } else {
            px->tok = T_STAR;
            px->cur++;
        }
        return;
    }

    /* 'res' token */
    if (px->cur[0] == 'r' && px->cur[1] == 'e' && px->cur[2] == 's') {
        char nx = px->cur[3];
        if (!isalnum((uint8_t)nx) && nx != '_') {
            px->tok = T_NUM;
            px->ival = last_result;
            px->dval = last_result_d;
            px->cur += 3;
            return;
        }
    }

    /* numeric literal */
    if (ibase_float) {
        char *end;
        errno = 0;
        px->dval = strtod(px->cur, &end);
        if (end == px->cur || errno) {
            snprintf(px->errmsg, sizeof(px->errmsg), "invalid token '%.20s'", px->cur);
            px->err = 1;
            px->tok = T_ERR;
            return;
        }
        px->cur = end;
        px->tok = T_NUM;
    } else {
        const char *q = px->cur;
        while (1) {
            int d;
            if (*q >= '0' && *q <= '9')
                d = *q - '0';
            else if (*q >= 'a' && *q <= 'f')
                d = *q - 'a' + 10;
            else if (*q >= 'A' && *q <= 'F')
                d = *q - 'A' + 10;
            else
                break;
            if (d >= ibase)
                break;
            q++;
        }
        if (q == px->cur) {
            snprintf(px->errmsg, sizeof(px->errmsg), "invalid token '%.20s'", px->cur);
            px->err = 1;
            px->tok = T_ERR;
            return;
        }
        char nbuf[64];
        size_t n = (size_t)(q - px->cur);
        if (n >= sizeof(nbuf))
            n = sizeof(nbuf) - 1;
        memcpy(nbuf, px->cur, n);
        nbuf[n] = 0;
        char *end;
        errno = 0;
        px->ival = (int64_t)strtoll(nbuf, &end, ibase);
        if (errno) {
            snprintf(px->errmsg, sizeof(px->errmsg), "number out of range");
            px->err = 1;
            px->tok = T_ERR;
            return;
        }
        px->cur = q;
        px->tok = T_NUM;
    }
}

/* Forward declarations needed for parenthesised sub-expressions. */
static int64_t px_expr_i(Parser *px);
static double px_expr_d(Parser *px);

/* ── integer recursive-descent ── */

/**
 * @brief Parse and evaluate a primary integer expression.
 *
 * Handles numeric literals, the @c res keyword, and parenthesised
 * sub-expressions.
 * @param px Parser state.
 * @return Evaluated integer value, or 0 on error.
 */
static int64_t px_primary_i(Parser *px) {
    if (px->err)
        return 0;
    if (px->tok == T_NUM) {
        int64_t v = px->ival;
        px_advance(px);
        return v;
    }
    if (px->tok == T_LP) {
        px_advance(px);
        int64_t v = px_expr_i(px);
        if (!px->err) {
            if (px->tok != T_RP) {
                snprintf(px->errmsg, sizeof(px->errmsg), "expected ')'");
                px->err = 1;
            } else {
                px_advance(px);
            }
        }
        return v;
    }
    snprintf(px->errmsg, sizeof(px->errmsg), "unexpected token");
    px->err = 1;
    return 0;
}

/**
 * @brief Parse and evaluate a unary integer expression.
 *
 * Handles leading @c + and @c - signs before delegating to
 * px_primary_i().
 * @param px Parser state.
 * @return Evaluated integer value, or 0 on error.
 */
static int64_t px_unary_i(Parser *px) {
    if (px->err)
        return 0;
    if (px->tok == T_MINUS) {
        px_advance(px);
        return -px_unary_i(px);
    }
    if (px->tok == T_PLUS) {
        px_advance(px);
        return px_unary_i(px);
    }
    return px_primary_i(px);
}

/**
 * @brief Parse and evaluate an integer exponentiation expression.
 *
 * Implements right-associative @c ^ (and @c **) using repeated squaring.
 * Reports an error for negative exponents.
 * @param px Parser state.
 * @return Evaluated integer value, or 0 on error.
 */
static int64_t px_power_i(Parser *px) {
    if (px->err)
        return 0;
    int64_t base = px_unary_i(px);
    if (!px->err && px->tok == T_CARET) {
        px_advance(px);
        int64_t exp = px_power_i(px); /* right-associative recursion */
        if (px->err)
            return 0;
        if (exp < 0) {
            snprintf(px->errmsg, sizeof(px->errmsg), "negative exponent");
            px->err = 1;
            return 0;
        }
        int64_t result = 1;
        while (exp > 0) {
            if (exp & 1)
                result *= base;
            exp >>= 1;
            if (exp > 0)
                base *= base;
        }
        return result;
    }
    return base;
}

/**
 * @brief Parse and evaluate an integer multiplicative expression.
 *
 * Handles @c *, @c /, and @c % with left-to-right associativity.
 * Reports errors for division and modulo by zero.
 * @param px Parser state.
 * @return Evaluated integer value, or 0 on error.
 */
static int64_t px_term_i(Parser *px) {
    if (px->err)
        return 0;
    int64_t lhs = px_power_i(px);
    while (!px->err && (px->tok == T_STAR || px->tok == T_SLASH || px->tok == T_PCT)) {
        int op = px->tok;
        px_advance(px);
        int64_t rhs = px_power_i(px);
        if (px->err)
            break;
        if (op == T_STAR) {
            lhs *= rhs;
        } else if (op == T_SLASH) {
            if (rhs == 0) {
                snprintf(px->errmsg, sizeof(px->errmsg), "division by zero");
                px->err = 1;
                break;
            }
            if (lhs == INT64_MIN && rhs == -1) {
                snprintf(px->errmsg, sizeof(px->errmsg), "overflow");
                px->err = 1;
                break;
            }
            lhs /= rhs;
        } else {
            if (rhs == 0) {
                snprintf(px->errmsg, sizeof(px->errmsg), "modulo by zero");
                px->err = 1;
                break;
            }
            lhs = (lhs == INT64_MIN && rhs == -1) ? 0 : lhs % rhs;
        }
    }
    return lhs;
}

/**
 * @brief Parse and evaluate a full integer additive expression.
 *
 * Handles @c + and @c - with left-to-right associativity.
 * @param px Parser state.
 * @return Evaluated integer value, or 0 on error.
 */
static int64_t px_expr_i(Parser *px) {
    if (px->err)
        return 0;
    int64_t lhs = px_term_i(px);
    while (!px->err && (px->tok == T_PLUS || px->tok == T_MINUS)) {
        int op = px->tok;
        px_advance(px);
        int64_t rhs = px_term_i(px);
        if (px->err)
            break;
        lhs = (op == T_PLUS) ? lhs + rhs : lhs - rhs;
    }
    return lhs;
}

/* ── floating-point recursive-descent ── */

/**
 * @brief Parse and evaluate a primary floating-point expression.
 *
 * Handles numeric literals, the @c res keyword, and parenthesised
 * sub-expressions.
 * @param px Parser state.
 * @return Evaluated double value, or 0.0 on error.
 */
static double px_primary_d(Parser *px) {
    if (px->err)
        return 0.0;
    if (px->tok == T_NUM) {
        double v = px->dval;
        px_advance(px);
        return v;
    }
    if (px->tok == T_LP) {
        px_advance(px);
        double v = px_expr_d(px);
        if (!px->err) {
            if (px->tok != T_RP) {
                snprintf(px->errmsg, sizeof(px->errmsg), "expected ')'");
                px->err = 1;
            } else {
                px_advance(px);
            }
        }
        return v;
    }
    snprintf(px->errmsg, sizeof(px->errmsg), "unexpected token");
    px->err = 1;
    return 0.0;
}

/**
 * @brief Parse and evaluate a unary floating-point expression.
 *
 * Handles leading @c + and @c - signs before delegating to
 * px_primary_d().
 * @param px Parser state.
 * @return Evaluated double value, or 0.0 on error.
 */
static double px_unary_d(Parser *px) {
    if (px->err)
        return 0.0;
    if (px->tok == T_MINUS) {
        px_advance(px);
        return -px_unary_d(px);
    }
    if (px->tok == T_PLUS) {
        px_advance(px);
        return px_unary_d(px);
    }
    return px_primary_d(px);
}

/**
 * @brief Parse and evaluate a floating-point exponentiation expression.
 *
 * Implements right-associative @c ^ (and @c **) via @c pow().
 * @param px Parser state.
 * @return Evaluated double value, or 0.0 on error.
 */
static double px_power_d(Parser *px) {
    if (px->err)
        return 0.0;
    double base = px_unary_d(px);
    if (!px->err && px->tok == T_CARET) {
        px_advance(px);
        double exp = px_power_d(px);
        return px->err ? 0.0 : pow(base, exp);
    }
    return base;
}

/**
 * @brief Parse and evaluate a floating-point multiplicative expression.
 *
 * Handles @c *, @c /, and @c % (via @c fmod) with left-to-right
 * associativity. Reports errors for division and modulo by zero.
 * @param px Parser state.
 * @return Evaluated double value, or 0.0 on error.
 */
static double px_term_d(Parser *px) {
    if (px->err)
        return 0.0;
    double lhs = px_power_d(px);
    while (!px->err && (px->tok == T_STAR || px->tok == T_SLASH || px->tok == T_PCT)) {
        int op = px->tok;
        px_advance(px);
        double rhs = px_power_d(px);
        if (px->err)
            break;
        if (op == T_STAR) {
            lhs *= rhs;
        } else if (op == T_SLASH) {
            if (rhs == 0.0) {
                snprintf(px->errmsg, sizeof(px->errmsg), "division by zero");
                px->err = 1;
                break;
            }
            lhs /= rhs;
        } else {
            if (rhs == 0.0) {
                snprintf(px->errmsg, sizeof(px->errmsg), "modulo by zero");
                px->err = 1;
                break;
            }
            lhs = fmod(lhs, rhs);
        }
    }
    return lhs;
}

/**
 * @brief Parse and evaluate a full floating-point additive expression.
 *
 * Handles @c + and @c - with left-to-right associativity.
 * @param px Parser state.
 * @return Evaluated double value, or 0.0 on error.
 */
static double px_expr_d(Parser *px) {
    if (px->err)
        return 0.0;
    double lhs = px_term_d(px);
    while (!px->err && (px->tok == T_PLUS || px->tok == T_MINUS)) {
        int op = px->tok;
        px_advance(px);
        double rhs = px_term_d(px);
        if (px->err)
            break;
        lhs = (op == T_PLUS) ? lhs + rhs : lhs - rhs;
    }
    return lhs;
}

/**
 * @brief Parse and evaluate a full expression, updating @c last_result on success.
 *
 * Supports multiple operations, operator precedence (+- < *\/% < ^), and
 * parentheses. Division/modulo by zero and signed overflow on division are
 * reported as errors to stderr. On success the result is stored in
 * @c last_result / @c last_result_d so it can be referenced as @c res.
 * @param line Null-terminated input line (whitespace already trimmed).
 */
static void handle_expression(const char *line) {
    Parser px;
    memset(&px, 0, sizeof(px));
    px.cur = line;
    px_advance(&px);

    if (ibase_float) {
        double result = px_expr_d(&px);
        if (px.err) {
            fprintf(stderr, "error: %s\n", px.errmsg);
            return;
        }
        if (px.tok != T_END) {
            fprintf(stderr, "error: unexpected input\n");
            return;
        }
        if (obase_float) {
            printf("%g\n", result);
        } else {
            print_result((int64_t)result);
        }
        last_result_d = result;
        last_result = (int64_t)result;
    } else {
        int64_t result = px_expr_i(&px);
        if (px.err) {
            fprintf(stderr, "error: %s\n", px.errmsg);
            return;
        }
        if (px.tok != T_END) {
            fprintf(stderr, "error: unexpected input\n");
            return;
        }
        if (obase_float) {
            printf("%g\n", (double)result);
        } else {
            print_result(result);
        }
        last_result = result;
        last_result_d = (double)result;
    }
}

/**
 * @brief Entry point — runs the interactive calculator REPL.
 *
 * Reads one line at a time from stdin. Special commands:
 *   - @c exit / @c quit — terminate the session.
 *   - @c ibase @c \<n\> — set the input base (2, 8, 10, 10f, 16).
 *   - @c obase @c \<n\> — set the output base (2, 8, 10, 10f, 16).
 *   - Any other input is passed to handle_expression().
 * The token @c res in any expression refers to the last computed result
 * (initially 0).
 * @return 0 on normal exit.
 */
int main(void) {
    char line[LINE_MAX_LEN];
    printf("calc %s — type 'exit' or 'quit' to leave\n", CALC_VERSION);

    while (1) {
        printf("> ");
        fflush(stdout); // flush stdout

        if (fgets(line, sizeof(line), stdin) == NULL)
            break; /* EOF */

        // line starts with ctrl+l
        if (line[0] == 12) {
            printf(CLEAR_SCREEN);
            continue;
        }

        char *s = trim(line);
        if (*s == 0)
            continue;

        if (strcmp(s, "exit") == 0 || strcmp(s, "quit") == 0)
            break;

        if (strcmp(s, "clear") == 0) {
            printf(CLEAR_SCREEN);
            continue;
        }

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
