# Calc

A simple interactive calculator for the terminal, inspired by `bc` but intentionally minimal. Supports basic arithmetic operations and configurable input/output bases (2, 8, 10, 16).

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

Enter an expression with two operands and one operator:

```
> 15+15
30
> 100-37
63
> 8*7
56
> 100/3
33
> 100%3
1
> 10^2
100
> 10**2
100
```

Supported operators: `+` `-` `*` `/` `%` `^` (also `**`)

Integer division truncates toward zero. Modulo follows the sign of the dividend. Exponentiation requires a non-negative integer exponent.

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

Hexadecimal digits must be uppercase (`A`–`F`).

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

Binary output is left-padded with zeros to the nearest byte boundary (8, 16, 32, or 64 bits):

```
> ibase 10
> obase 2
> 8
00001000
> 255
11111111
> 256
0000000100000000
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

### Error handling

| Condition | Message |
|---|---|
| Division by zero | `error: division by zero` |
| Modulo by zero | `error: modulo by zero` |
| Arithmetic overflow | `error: overflow` |
| Invalid digit for current base | `error: invalid left/right operand: …` |
| Negative exponent | `error: negative exponent` |
| Unsupported base | `error: ibase must be 2, 8, 10, 10f or 16` |
| Unrecognized input | `error: unrecognized input: …` |

## Limits

Operands and results are `long long` (64-bit signed integer, range −2^63 to 2^63−1). There is no arbitrary-precision arithmetic.

## License

See [LICENSE](LICENSE).
