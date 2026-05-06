# Calc

A simple interactive calculator for the terminal, inspired by `bc` but intentionally minimal. Supports multi-operation expressions with full operator precedence and parentheses, and configurable input/output bases (2, 8, 10, 16).

## Binary

A pre-built statically compiled binary is available in the `bin/` folder.

## Build

```sh
make
```

Requires a C99-capable compiler (gcc or clang). No external dependencies.

## Usage

```sh
./calc
```

The prompt `> ` appears and the calculator waits for input. Type `exit` or `quit` (or send EOF with Ctrl-D) to leave.

### Arithmetic

Expressions can contain multiple operators. Standard precedence applies — `^` binds tightest, then `* / %`, then `+ -`. Use parentheses to override:

```
> 10+10+10
30
> 2+3*4
14
> (2+3)*4
20
> 10/2+3*4-1
16
> 15+15
30
> 100/3
33
> 100%3
1
> 10^2
100
> 2^3^2
512
```

Supported operators: `+` `-` `*` `/` `%` `^` (also `**`)

`^` is right-associative (`2^3^2` = `2^(3^2)` = 512). Integer division truncates toward zero. Modulo follows the sign of the dividend. Exponentiation requires a non-negative integer exponent.

### Result variable

The last computed result is stored in `res` and can be used anywhere a number is expected in any subsequent expression:

```
> 1+1
2
> res+1
3
> res*res
9
> res*2+1
19
> -3
-3
> res+10
7
```

`res` starts at `0` and is updated after every successful computation, including in floating-point mode. Use it alone to echo or repeat a base conversion.

### Base conversion

`ibase` sets the input base, `obase` sets the output base. Both default to 10. Supported values: `2`, `8`, `10`, `10f`, `16`.

#### Floating-point mode (`10f`)

Use `10f` (case-insensitive) as the base to switch a channel to IEEE-754 double precision.

- `ibase 10f` — parse inputs as floating-point numbers; arithmetic uses `double`.
- `obase 10f` — format the result as a floating-point number.

```
> ibase 10f
> obase 10f
> 1.5+2.5
4
> 10.0/3.0
3.33333
> 2.0^0.5
1.41421
> 3.5%1.5
0.5
```

Exponentiation with a `double` exponent (`2.0^0.5` = √2) and `fmod` for `%` are supported. Division and modulo by zero are still reported as errors.

Hexadecimal digits may be uppercase or lowercase (`A`–`F` / `a`–`f`).

```
> ibase 16
> obase 16
> 9+1
A
```

```
> ibase 16
> obase 10
> A
10
```

Binary output is left-padded with zeros to the nearest byte boundary (8, 16, 32, or 64 bits). Negative values are shown in two's complement:

```
> ibase 10
> obase 2
> 8
00001000
> 255
11111111
> 256
0000000100000000
> -2
11111110
> -128
10000000
> -129
1111111101111111
```

```
> ibase 10
> obase 8
> 255
377
```

A bare number with no operator performs a plain base conversion:

```
> ibase 16
> obase 10
> FF
255
```

### Clear screen

Type `clear` to clear the terminal screen:

```
> clear
```

### Error handling

| Condition | Message |
|---|---|
| Division by zero | `error: division by zero` |
| Modulo by zero | `error: modulo by zero` |
| Arithmetic overflow | `error: overflow` |
| Invalid digit / token | `error: invalid token '…'` |
| Negative exponent | `error: negative exponent` |
| Missing closing parenthesis | `error: expected ')'` |
| Unsupported base | `error: ibase must be 2, 8, 10, 10f or 16` |

## Limits

Operands and results are `long long` (64-bit signed integer, range −2^63 to 2^63−1). There is no arbitrary-precision arithmetic.

## License

See [LICENSE](LICENSE).
