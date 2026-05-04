# calc

A simple interactive calculator for the terminal, inspired by `bc` but intentionally minimal. Supports the five basic arithmetic operations and configurable input/output bases (2, 8, 10, 16).

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
```

Supported operators: `+` `-` `*` `/` `%`

Integer division truncates toward zero. Modulo follows the sign of the dividend.

### Base conversion

`ibase` sets the input base, `obase` sets the output base. Both default to 10. Supported values: `2`, `8`, `10`, `16`.

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

```
> ibase 10
> obase 2
> 255
11111111
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
| Unsupported base | `error: ibase must be 2, 8, 10 or 16` |
| Unrecognized input | `error: unrecognized input: …` |

## Limits

Operands and results are `long long` (64-bit signed integer, range −2^63 to 2^63−1). There is no arbitrary-precision arithmetic.

## License

See [LICENSE](LICENSE).
