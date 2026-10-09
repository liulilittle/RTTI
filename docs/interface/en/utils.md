# RTTI — Tools & Support Cluster: Interface Reference

> Edition: English. 简体中文版见 [../zh/utils.md](../zh/utils.md).
>
> **Accuracy contract.** Every signature below is quoted verbatim from the
> header files. Each chapter states the exact line range it was extracted from.
> Example values come from the repository's own test assertions
> (`tests/base_convert_test.cpp`, `tests/date_time_test.cpp`,
> `tests/naming_config_test.cpp`, `tests/type_support_test.cpp`) — no invented
> numbers. Implementation location is cross-referenced into `src/` where the
> declared functions are actually defined.

**Headers covered**

| Header | Scope | Extraction range |
| --- | --- | --- |
| `include/rtti/base_convert.hpp` | arbitrary-base integer ↔ string | lines 1–112 |
| `include/rtti/date_time.hpp` | Unix-epoch nanosecond timestamp value type | lines 1–247 |
| `include/rtti/naming.hpp` | reflected-field key naming policy | lines 1–53 |
| `include/rtti/version.hpp` | version macros + `rtti::version()` | lines 1–6 |
| `include/rtti/rtti_fwd.hpp` | forward declarations + feature-gate defaults | lines 1–45 |

Related converter-bridge entry points that live in `include/rtti/convert.hpp`
(`to_value_dt`, `date_time_output`, `to_value_base` / `from_value_base`) are
documented in the chapters whose value types they bridge, because that is the
piece of the API surface a caller of `date_time` / `base_convert` actually
reaches for.

---

## 1. Global entry points & build model

### 1.1 How to include

Every public header is included with the tree path rooted at `include/`:

```cpp
#include <rtti/base_convert.hpp>   // rtti::detail::to_base / from_base …
#include <rtti/date_time.hpp>      // rtti::date_time, rtti::epoch_ns_from_digits
#include <rtti/naming.hpp>         // rtti::detail::key_case / naming_policy …
#include <rtti/version.hpp>        // RTTI_VERSION_* + rtti::version()
#include <rtti/rtti_fwd.hpp>       // version_tag + RTTI_ENABLE_* defaults
```

The public API lives in namespace `rtti`; the conversion/naming helpers live
two layers deep in `rtti::detail` and are documented exactly there.

### 1.2 Header-only or not — factual statement

**This cluster is not header-only.** The headers in scope declare functions
whose definitions are compiled into the static library:

| Header | Implementation unit in `src/` |
| --- | --- |
| `base_convert.hpp` | `src/base_convert.cpp` |
| `date_time.hpp` | `src/date_time.cpp` |
| `naming.hpp` | `src/naming.cpp` |

`README.md` (lines 24–37) states: `src/` — "static-library sources", i.e. the
reflect core alone (with every format-adapter gate off) still links against a
static library. The CMake target is `rtti` (`add_library(rtti STATIC …)`,
`CMakeLists.txt:22`), exposed as `rtti::rtti` with a `BUILD_INTERFACE`/
`INSTALL_INTERFACE` include directory (`CMakeLists.txt:58–60`, `97`). A consumer
must link the library and may then `#include <rtti/…>`.

The header-only exceptions inside this cluster are genuinely inline:

- `rtti::version()` — `inline`, defined in `version.hpp:6`;
- `rtti::epoch_ns_from_digits` — `inline`, defined in `date_time.hpp:192–245`;
- the `date_time` comparison/arithmetic friend operators and the
  `is_date_time` trait — defined inline in `date_time.hpp:105–186`.

### 1.3 Shared conventions (spec §0)

- Failure is reported through `rtti::error_code` / `rtti::result<T>`; input
  that cannot be handled produces an error code and **never crashes, never
  wraps silently**. The one documented exception is `to_base`, which has no
  error channel and returns an empty string instead (see §2.3).
- `rtti::error_code` values referenced by these headers include
  `ok`, `invalid_number`, `overflow`, `unexpected_token`,
  `kind_mismatch`, `no_such_field` (`error.hpp:14–39`).
- `rtti::string_view` is a plain alias for `std::string_view`
  (`memory.hpp:18`).
- C++17; no third-party dependencies.

---

## 2. `base_convert.hpp` — arbitrary-base integer ↔ string conversion

> Extraction range: `include/rtti/base_convert.hpp` lines 1–112.
> Implementation: `src/base_convert.cpp` lines 1–206.

Converts a `uint64_t` to/from a digit string in any base **2..64**
(spec §15). `to_base_signed` / `from_base_signed` add a leading `'-'` for
`int64_t`. The digit table is configurable; overflow / illegal-character /
invalid-base inputs return an `error_code` and never crash.

### 2.1 Digit tables

Default table (spec §15.2) — `src/base_convert.cpp:19–23`:

```
"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+/"
```

Symbol `i` is the digit for value `i`, so the first `base` symbols form that
base's alphabet:

- base 2..36 → `0-9a-z` (strtol-compatible lowercase set);
- base 37..62 → `0-9a-zA-Z` (62 symbols);
- base 63..64 → the 62 symbols plus `'+'` at index 62 and `'/'` at index 63.

RFC 4648 base64 ordering is available as a ready table
(`src/base_convert.cpp:25–29`):

```
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
```

#### `const char* default_digits() noexcept;` — `base_convert.hpp:47`

Returns the default table described above (stable pointer, never `nullptr`).

#### `const char* rfc4648_digits() noexcept;` — `base_convert.hpp:52`

Returns the RFC 4648 base64 ordering. Supply it explicitly as the `digits`
argument of any function below to opt into that ordering for any base up to 64.

#### `std::size_t digit_table_len(const char* digits) noexcept;` — `base_convert.hpp:55`

Number of symbols in a table; `0` when `digits` is `nullptr`.

#### `bool default_case_insensitive(unsigned base) noexcept;` — `base_convert.hpp:61`

`true` iff `base <= 36`. This is the strtol-compatible rule: for bases 2..36
the lowercase default table matches letters case-insensitively (`"FF"` parses
as 255). Custom tables default to strict case-sensitive parsing; callers opt
into insensitivity explicitly (see the 5-argument `from_base`).

#### `int base_digit(char c, const char* digits, std::size_t base, bool insensitive) noexcept;` — `base_convert.hpp:67`

Digit value of byte `c` within the first `base` symbols of `digits`, or `-1`
when `c` is not one of them. When `insensitive` is set, a byte with no exact
match is retried in the opposite ASCII letter case (`'A'` resolves against a
lowercase-only table and vice versa). Implementation `src/base_convert.cpp:42–63`.

### 2.2 Case policy (summary)

- base ≤ 36, default table: **case-insensitive** letters,
  `from_base("FF", 16, out)` == 255.
- base > 36: **case-sensitive** — `'a'` and `'A'` are distinct digits
  (`'a'`=10, `'A'`=36 in base 62).
- custom tables: case-sensitive unless the explicit `case_insensitive` flag is
  passed.

### 2.3 `to_base` / `to_base_signed` — integer → string

```cpp
std::string to_base(std::uint64_t value, unsigned base,
                    const char* digits = nullptr);        // base_convert.hpp:74
std::string to_base_signed(std::int64_t value, unsigned base); // base_convert.hpp:79
```

- **Parameters** — `value`: the integer to convert; `base`: 2..64;
  `digits`: optional symbol table with at least `base` symbols
  (defaults to `default_digits()`).
- **Returns** — a fresh `std::string`. **Failure signal:** an invalid base
  (0, 1, > 64) or an undersized table yields an **empty string**, because these
  functions have no `error_code` channel. Zero converts to a single `"0"`.
  `to_base_signed` prefixes `'-'` for negative values and propagates the empty
  string on invalid bases.
- **Notes** — `INT64_MIN`'s magnitude (2^63) is handled via the two's
  complement magnitude idiom, so it round-trips exactly
  (`src/base_convert.cpp:94–108`).

**Examples** (verbatim assertions from `tests/base_convert_test.cpp:40–56,
107–118`):

```cpp
bc::to_base(0u, 2)   == "0"
bc::to_base(0u, 64)  == "0"
bc::to_base(255u, 16) == "ff"
bc::to_base(255u, 8)  == "377"
bc::to_base(255u, 2)  == "11111111"
bc::to_base(255u, 36) == "73"
bc::to_base(255u, 64) == "3/"     // '/' is the 64th symbol
bc::to_base(255u, 63) == "43"
bc::to_base(42u, 62)  == "G"      // 'G' = uppercase index 32
bc::to_base(4095u, 36) == "35r"
bc::to_base(46655u, 36) == "zzz"
bc::to_base(3843u, 62) == "ZZ"    // 'A'(36) vs 'a'(10) distinct in base 62

bc::to_base_signed(-255, 16)                         == "-ff"
bc::to_base_signed(INT64_MIN, 10)                    == "-9223372036854775808"
bc::to_base_signed(INT64_MAX, 10)                    == "9223372036854775807"
bc::to_base_signed(INT64_MIN, 2)                     == "-1" + std::string(63, '0')
```

Invalid base on the output side (`base_convert_test.cpp:201–204`);
undersized table (`:236`):

```cpp
bc::to_base(255u, 0)  == ""      // invalid base
bc::to_base(255u, 1)  == ""
bc::to_base(255u, 65) == ""
bc::to_base(255u, 70) == ""
bc::to_base(255u, 32, kBaseConvertShort) == ""   // table shorter than base
```

### 2.4 `from_base` — string → unsigned integer (4 overloads)

```cpp
error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out, const char* digits,
                     bool case_insensitive);                // base_convert.hpp:87  (master)
error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out);                   // base_convert.hpp:92  (default table, strtol case policy)
error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out, bool case_insensitive); // base_convert.hpp:96 (default table, explicit flag)
error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out, const char* digits);    // base_convert.hpp:100 (custom table, case-sensitive)
```

- **Parameters** — `s`: digit string; `base`: 2..64; `out`: result;
  `digits`: optional table (default `default_digits()`); `case_insensitive`:
  enable the swap-case fallback in `base_digit`.
- **Returns** — `error_code::ok` and sets `out` on success. Otherwise:
  - `invalid_number` — invalid base (0, 1, > 64), empty input, a character
    outside the base's digit set, or an undersized custom table;
  - `overflow` — the value would exceed `UINT64_MAX`.
  The accumulator is checked **before** multiplication
  (`acc > (UINT64_MAX - d) / base`), so an overflowing lane is detected
  instead of wrapped (`src/base_convert.cpp:126–140`).
- **Overload semantics** — the 3-argument form uses the default table and the
  strtol case policy (`default_case_insensitive(base)`). The 4-argument `bool`
  form keeps the default table and sets the flag. The 4-argument `const char*`
  form uses a custom table case-sensitively. The master form takes both
  explicitly.

**Examples** — known values and round-trips (`base_convert_test.cpp:244–249`):

```cpp
from_base("ff", 16, out) == ok, out == 255
from_base("FF", 16, out) == ok, out == 255     // strtol case-insensitive, base ≤ 36
from_base("DeAdBeEf", 16, out) == ok, out == 0xDEADBEEF
```

Base > 36 is case-sensitive (`:251–254`):

```cpp
from_base("a", 62, out) == ok, out == 10
from_base("A", 62, out) == ok, out == 36
```

Overflow (never wrapping) — `:146–168`:

```cpp
from_base("18446744073709551616", 10, out) == overflow    // 2^64 exactly
from_base("18446744073709551615", 10, out) == ok, out == UINT64_MAX
from_base(std::string(64, '1'), 2, out) == ok             // all 64 bits fit
from_base(std::string(65, '1'), 2, out) == overflow
from_base(std::string(16, 'f'), 16, out) == ok            // 2^64 - 1
from_base(std::string(17, 'f'), 16, out) == overflow
```

Invalid input — `:181–199`:

```cpp
from_base("10", 0, out)  == invalid_number   // base must be 2..64
from_base("10", 1, out)  == invalid_number
from_base("10", 65, out) == invalid_number
from_base("", 10, out)   == invalid_number   // empty input
from_base("1g", 16, out) == invalid_number   // 'g' not a hex digit
from_base("1a", 10, out) == invalid_number
from_base("102", 2, out) == invalid_number
from_base("3/", 63, out) == invalid_number   // '/' is the 64th symbol, out of range for base 63
from_base("z", 35, out)  == invalid_number   // base 35 stops at 'y'
from_base(" ", 10, out)  == invalid_number
from_base("!", 64, out)  == invalid_number
```

### 2.5 `from_base_signed` — string → signed integer

```cpp
error_code from_base_signed(std::string_view s, unsigned base,
                            std::int64_t& out);            // base_convert.hpp:108
```

- **Parameters** — `s`: signed digit string; `base`: 2..64; `out`: result.
- **Returns** — `error_code::ok` with `out` set; `invalid_number` for an
  invalid base, empty input, a bare or doubled sign (`"-"`, `"+"`, `"--1"`,
  misplaced `"1-"`), or illegal characters; `overflow` when the magnitude
  exceeds `INT64_MAX` (positive) or 2^63 (negative).
- **Notes** — an optional `'+'`/`'-'` prefix is accepted (strtol-like); the
  magnitude is parsed through `from_base` with the default table and strtol
  case policy. `INT64_MIN` is representable exactly through its 2^63 magnitude
  (`src/base_convert.cpp:157–203`).

**Examples** (`base_convert_test.cpp:121–128, 170–178, 208–219`):

```cpp
from_base_signed("-9223372036854775808", 10, back) == ok, back == INT64_MIN
from_base_signed("9223372036854775807", 10, back) == ok, back == INT64_MAX
from_base_signed("-1", 2, back)                    == ok, back == -1

from_base_signed("9223372036854775808", 10, so)   == overflow   // INT64_MAX + 1
from_base_signed("-9223372036854775809", 10, so)  == overflow   // -(2^63) - 1

from_base_signed("-", 10, so)  == invalid_number   // bare sign
from_base_signed("+", 10, so)  == invalid_number
from_base_signed("--1", 10, so) == invalid_number  // doubled sign
from_base_signed("1-", 10, so) == invalid_number   // misplaced sign
from_base_signed("", 10, so)   == invalid_number
```

### 2.6 Custom digit tables

Pass any `digits` table with at least `base` symbols. URL-safe base64 symbols
(`base_convert_test.cpp:33–38, 222–239`):

```cpp
static const char kUrlSafe[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_";

to_base(255u, 64, kUrlSafe)       == "3_"
to_base(62u, 64, kUrlSafe)        == "-"
to_base(63u, 64, kUrlSafe)        == "_"
to_base(4095u, 64, kUrlSafe)      == "__"
from_base("3_", 64, back, kUrlSafe) == ok, back == 255
```

A table shorter than the base is rejected on **both** sides — `to_base`
returns `""`, `from_base` returns `invalid_number`
(`base_convert_test.cpp:236–238`).

Custom-table case handling (`base_convert_test.cpp:257–264`) with an
uppercase-only table:

```cpp
from_base("FF", 36, out, kUpper)       == ok, out == 555
from_base("ff", 36, out, kUpper)       == invalid_number       // case-sensitive default
from_base("ff", 36, out, kUpper, true) == ok, out == 555       // explicit flag
```

### 2.7 Converter bridge (defined in `convert.hpp`, spec §15.4)

The value-tree serialization bridge for the base-N integer lane is declared in
`include/rtti/convert.hpp:572–620` (inline):

```cpp
error_code to_value_base(value_io* dst, std::uint64_t value, unsigned base, const char* digits = nullptr);
error_code from_value_base(const value_io* src, unsigned base, std::uint64_t& out);
error_code to_value_base_signed(value_io* dst, std::int64_t value, unsigned base);
error_code from_value_base_signed(const value_io* src, unsigned base, std::int64_t& out);
```

`to_value_*` writes the base-N string as a text scalar (`invalid_number` when
the base is invalid); `from_value_*` reads a text scalar back
(`kind_mismatch` for a non-text node). These are thin wrappers over the
functions documented above.

---

## 3. `date_time.hpp` — Unix-epoch nanosecond timestamp

> Extraction range: `include/rtti/date_time.hpp` lines 1–247.
> Implementation: `src/date_time.cpp` lines 1–398.

`rtti::date_time` is a value type holding **Unix epoch nanoseconds** as a
single `int64_t`, with ISO 8601 parsing/formatting and precision-aware unit
conversion (spec §14).

Core design (header comment, `date_time.hpp:5–19`):

- `int64` nanosecond storage covers roughly **1677..2262** at ns resolution;
- `from_unix_*` range-check the scaled product and report
  `error_code::overflow` instead of ever wrapping (spec §0);
- `parse()` accepts the ISO 8601 subset
  `YYYY-MM-DDTHH:MM:SS[.frac][Z|+/-HH:MM]` (fraction 1..9 digits, offsets
  normalized to UTC) and returns `error_code::invalid_number` on malformed
  text;
- `format()` / `to_string()` emit the canonical UTC form, exact round-trip;
- `format(string_view pattern)` emits a strftime-style subset (spec §7.1);
  an unknown or malformed directive reports `error_code::unexpected_token`
  (the string overload returns an empty string) — fail-safe, never a crash;
- numeric epoch → ns is inferred from the decimal digit count (10 digits =
  seconds, 13 = ms, 16 = us, 19/20 = ns) via `epoch_ns_from_digits`.

### 3.1 Construction

```cpp
date_time() noexcept;                      // date_time.hpp:37  — epoch zero (1970-01-01T00:00:00Z)
explicit date_time(std::int64_t epoch_ns) noexcept;  // date_time.hpp:39 — exact ns count
```

```cpp
static result<date_time> from_unix_seconds(std::int64_t seconds); // :43
static result<date_time> from_unix_ms(std::int64_t ms);           // :44
static result<date_time> from_unix_us(std::int64_t us);           // :45
static result<date_time> from_unix_ns(std::int64_t ns);           // :46
```

- **Parameters** — the Unix epoch in the named unit.
- **Returns** — `result<date_time>`; the scaled nanosecond product is
  range-checked, and a value whose product would leave the `int64` ns lane
  yields `error_code::overflow` (`src/date_time.cpp:21–44`).
- **Boundaries** (from implementation):

| Factory | Acceptable input range | Resulting ns |
| --- | --- | --- |
| `from_unix_seconds` | ±9'223'372'036 | × 10^9 |
| `from_unix_ms` | ±9'223'372'036'854 | × 10^6 |
| `from_unix_us` | ±9'223'372'036'854'775 | × 10^3 |
| `from_unix_ns` | any `int64_t` | verbatim |

**Examples** (`date_time_test.cpp:41–94`):

```cpp
auto s = date_time::from_unix_seconds(1791290096);
s.ok() && s.value.to_unix_ms(ms) == ok && ms == 1791290096000;
s.value.to_unix_ns(ns) == ok && ns == 1791290096 * 1000000000LL;

from_unix_seconds(9223372036).ok()  == true    // boundary, ok
from_unix_seconds(9223372037).err.code == overflow
from_unix_ms(9223372036855).err.code     == overflow
from_unix_us(9223372036854776).err.code  == overflow
from_unix_us(9223372036854775).ok()      == true
from_unix_seconds(-9223372037).err.code  == overflow
from_unix_ns(INT64_MAX).ok()             == true
```

### 3.2 Unit accessors

```cpp
error_code to_unix_seconds(std::int64_t& out) const noexcept; // :52
error_code to_unix_ms(std::int64_t& out) const noexcept;      // :53
error_code to_unix_us(std::int64_t& out) const noexcept;      // :54
error_code to_unix_ns(std::int64_t& out) const noexcept;      // :55
std::int64_t epoch_ns() const noexcept;                       // :58
```

- **Parameters** — `out`: receives the instant in the requested unit.
- **Returns** — always `error_code::ok`; none of these can overflow the
  `int64` output because the divisor is at least 1000.
- **Notes** — sub-second counts use **floor division toward negative
  infinity**, so a pre-epoch instant with a fractional part maps to the whole
  second that actually contains it. For example
  `date_time(INT64_MIN).to_unix_seconds()` == -9'223'372'037 (one second below
  the truncated -9'223'372'036) (`date_time_test.cpp:246–254`).
- `epoch_ns()` returns the raw nanosecond count.

### 3.3 Canonical ISO 8601 text

```cpp
std::string format() const;      // :63   canonical UTC form
std::string to_string() const;   // :65   alias of format()
```

- **Notes** — whole seconds format as `YYYY-MM-DDTHH:MM:SSZ`; a nonzero
  sub-second part appends the shortest `".fff"` / `".ffffff"` /
  `".fffffffff"` fraction that carries the exact value. Exact round-trip with
  `parse()`.

**Examples** (`date_time_test.cpp:100–154, 238–254`):

```cpp
date_time().format() == "1970-01-01T00:00:00Z"
parse("2026-10-06T12:34:56Z").value.format()        == "2026-10-06T12:34:56Z"
parse("2026-10-06T12:34:56.123Z").value.format()    == "2026-10-06T12:34:56.123Z"
parse("2026-10-06T12:34:56.123456Z").value.format() == "2026-10-06T12:34:56.123456Z"
parse("2026-10-06T12:34:56.123456789Z").value.format() == "2026-10-06T12:34:56.123456789Z"
date_time(INT64_MIN).format() parses back to INT64_MIN (floor seconds == -9223372037)
```

### 3.4 strftime-style pattern formatting

```cpp
error_code format(string_view pattern, std::string& out) const;  // :91
std::string format(string_view pattern) const;                   // :95
```

- **Parameters** — `pattern`: format string; `out` (out-param overload):
  destination buffer.
- **Returns** — out-param overload: `error_code::ok`, or
  `error_code::unexpected_token` for an unknown directive, a trailing lone
  `'%'`, or any malformed pattern; on error `out` holds the partial text
  produced before the failure (it **appends**, never clears). String overload:
  formatted text, or an empty string on any unsupported directive.
- **Supported directives** (`date_time.hpp:68–81`):

| Directive | Output |
| --- | --- |
| `%Y` | year as decimal, at least four digits |
| `%y` | year mod 100, two digits |
| `%m` | month 01..12 |
| `%d` | day of month 01..31 |
| `%H` | hour 00..23 |
| `%M` | minute 00..59 |
| `%S` | second 00..59 |
| `%f` | nanosecond fraction, exactly nine digits |
| `%F` | `%Y-%m-%d` |
| `%T` | `%H:%M:%S` |
| `%R` | `%H:%M` |
| `%z` | UTC offset, `"+0000"` |
| `%Z` | timezone name, `"UTC"` |
| `%%` | a literal `'%'` |

Every other byte is copied verbatim. The instant is always UTC-normalized
(offsets never survive a parse), so `%z` and `%Z` are constants.

### 3.5 `parse`

```cpp
static result<date_time> parse(string_view iso);    // :102
```

- **Parameters** — `iso`: ISO 8601 subset
  `YYYY-MM-DDTHH:MM:SS[.frac][Z | +/-HH:MM]`.
- **Returns** — `result<date_time>`:
  - `error_code::ok` with the parsed instant;
  - `invalid_number` on any malformed text (never throws);
  - `overflow` when the resulting instant leaves the int64 ns range.
- **Notes** (`src/date_time.cpp:186–282`) — fractional digits 1..9 are kept
  verbatim as nanoseconds (10+ digits rejected). Timezone designators accepted:
  `Z` or `z` for UTC, `+/-HH:MM` for offsets (hour 00..23, minute 00..59),
  normalized to UTC. Calendar rules are honored (leap years, month lengths,
  hour 00..23). The whole-second lane must stay within the extended bounds
  `-9223372037 .. +9223372036` seconds (the extra negative second is the
  floor of `INT64_MIN / 1e9`), with exact sub-second boundary checks.

**Examples** (`date_time_test.cpp:96–160, 205–235`):

```cpp
parse("2026-10-06T12:34:56Z").value.epoch_ns()        == 1791290096 * 1e9
parse("2026-10-06T20:34:56+08:00").value.epoch_ns()   == same instant   // offset → UTC
parse("2026-10-06T04:34:56-08:00").value.epoch_ns()   == same instant
parse("2026-10-06T12:34:56+00:00").value.epoch_ns()   == same instant
parse("2026-10-06T20:34:56.5+08:00").value.epoch_ns() == 1791290096500000000
parse("1970-01-01T00:00:00Z").value.epoch_ns()        == 0
parse("1969-12-31T23:59:59Z").value.epoch_ns()        == -1000000000
parse("1969-12-31T23:59:58.5Z").value.epoch_ns()      == -1500000000   // floor division
parse("2024-02-29T00:00:00Z").ok()                    == true          // leap year

parse("9999-12-31T23:59:59Z").err.code == overflow       // beyond int64 ns range
parse("2026-10-06").ok()        == false                 // malformed
parse("2026-13-01T00:00:00Z")    == invalid_number       // month 13
parse("2023-02-29T00:00:00Z")    == invalid_number       // not a leap year
parse("2026-10-06T12:34:56")     == invalid_number       // missing timezone
parse("2026-10-06T12:34:56Zextra") == invalid_number     // trailing junk
parse("2026-10-06T12:34:56.1234567890Z") == invalid_number  // 10 fraction digits
parse("2026-10-06T12:34:56+25:00")     == invalid_number // offset hour out of range
parse("2026-10-06T12:34:56+08:0X")     == invalid_number // non-digit offset minute
```

### 3.6 Comparisons and arithmetic

```cpp
friend bool operator==/!=/</<=/>/>= (const date_time& a, const date_time& b) noexcept; // :105–122 (nanosecond exact)
friend date_time operator+(date_time t, std::int64_t ns) noexcept;  // :128
friend date_time operator-(date_time t, std::int64_t ns) noexcept;  // :133
friend std::int64_t operator-(const date_time& a, const date_time& b) noexcept; // :139
```

- The comparison operators compare the nanosecond counts exactly.
- Arithmetic is performed on unsigned counts so an out-of-range operand
  deterministically wraps (defined behavior, never signed overflow); callers
  wanting strict range semantics should use the checked `from_unix_*` /
  `epoch_ns_from_digits` paths (`date_time.hpp:124–143`).

**Examples** (`date_time_test.cpp:257–276`):

```cpp
a = from_unix_seconds(5).value; c = from_unix_seconds(8).value;
a == b;  a != c;  a < c;  c > a;  a <= b;  a >= b;
(a + 1000000000).epoch_ns() == 6000000000;
(c - 1000000000).epoch_ns() == 7000000000;
(c - a)                     == 3000000000;
```

### 3.7 `is_date_time`

```cpp
template <typename T>
struct is_date_time : std::bool_constant<std::is_same<T, date_time>::value> {};  // :185–186
```

`true` exactly for `rtti::date_time`. The convert dispatch uses it to route
`date_time` to the scalar converter even if a user also registers the type
with `RTTI_DEFINE_TYPE`.

### 3.8 `epoch_ns_from_digits` — digit-count unit inference

```cpp
inline error_code epoch_ns_from_digits(std::int64_t value,
                                       std::int64_t& out) noexcept;  // :192–193
```

- **Parameters** — `value`: integer Unix epoch; `out`: nanoseconds.
- **Returns** — `error_code::ok` and sets `out`, or:
  - `invalid_number` — a digit count that is none of 10/13/16/19/20
    (a leading sign is not counted; zero maps to epoch zero, seconds);
  - `overflow` — `value * factor` would leave the int64 ns range.
- **Unit table** (implementation, `date_time.hpp:212–239`):

| Digit count | Factor | Valid `value` window |
| --- | --- | --- |
| 10 | 1'000'000'000 | −9'223'372'036 … +9'223'372'036 |
| 13 | 1'000'000 | −9'223'372'036'854 … +9'223'372'036'854 |
| 16 | 1'000 | −9'223'372'036'854'775 … +9'223'372'036'854'775 |
| 19 | 1 | `INT64_MIN` … `INT64_MAX` |
| 20 | 1 | `INT64_MIN` … `INT64_MAX` |

The **ns lane spans the whole int64 range** — both `INT64_MIN` and
`INT64_MAX` are valid 19-digit ns values. The 10/13/16-digit lanes stay within
a symmetric ±bound window where `value * factor` cannot leave int64.

**Examples** (`date_time_test.cpp:162–203`):

```cpp
epoch_ns_from_digits(1789263451, ns)        == ok, ns == 1789263451000000000        // 10 digits → s
epoch_ns_from_digits(1789263451012, ns)     == ok, ns == 1789263451012000000        // 13 digits → ms
epoch_ns_from_digits(1789263451012345, ns)  == ok, ns == 1789263451012345000        // 16 digits → us
epoch_ns_from_digits(1789263451012345678, ns) == ok, ns == 1789263451012345678      // 19 digits → ns
epoch_ns_from_digits(-1789263451, ns)       == ok, ns == -1789263451000000000       // sign not counted
epoch_ns_from_digits(0, ns)                 == ok, ns == 0                          // zero → epoch zero
epoch_ns_from_digits(INT64_MIN, ns)         == ok, ns == INT64_MIN                  // full ns lane
epoch_ns_from_digits(INT64_MAX, ns)         == ok, ns == INT64_MAX

epoch_ns_from_digits(123456789, ns)    == invalid_number   // 9 digits
epoch_ns_from_digits(123456789012, ns) == invalid_number   // 12 digits
epoch_ns_from_digits(1, ns)            == invalid_number
epoch_ns_from_digits(9223372037, ns)   == overflow         // one past the 10-digit peak
epoch_ns_from_digits(9999999999999, ns) == overflow        // 13-digit lane overflow
```

### 3.9 Converter bridge (defined in `convert.hpp`)

The read path of `date_time` through `value_io` is the `converter<date_time>`
specialization (`convert.hpp:447–483`):

- **read** (`from_value(src, dt)`): an integer scalar's unit is inferred via
  `epoch_ns_from_digits`; an ISO text scalar is parsed via `parse()`;
  any other node kind → `kind_mismatch`; malformed text → `invalid_number`.
- **write** (`to_value(dst, dt)`): numeric epoch **seconds** by default.

Explicit output modes:

```cpp
enum class date_time_output { numeric_seconds, numeric_ms, numeric_us, numeric_ns, iso_string };  // convert.hpp:439–445

inline error_code to_value_dt(value_io* dst, const date_time& in,
                              date_time_output mode);          // convert.hpp:490–491
```

- `to_value_dt` writes the scalar in the requested unit; `iso_string` emits the
  canonical `format()` text (preserves sub-second precision exactly).

> **Note (discrepancy with the task list).** `rtti::from_value_dt` **does not
> exist** in the codebase. The read (node → `date_time`) path is the generic
> `from_value` / `converter<date_time>` shown above, which auto-detects the
> unit. Only `to_value_dt` exists as a dedicated entry point.

- **Reflected members**: a `date_time` member is serialized through the
  ISO-string lane (`member_to_value`, `convert.hpp:543–561`) so
  sub-second precision survives reflected-struct round-trips.

**Examples** (`date_time_test.cpp:278–378`):

```cpp
// read: numeric scalars auto-detect the unit
from_value(sec_node, dt) == ok, dt.epoch_ns() == 1791290096 * 1e9;      // 10 digits
from_value(ms_node,  dt) == ok, dt.epoch_ns() == 1791290096123000000;   // 13 digits
from_value(ns_node,  dt) == ok, dt.epoch_ns() == 1791290096123456789;   // 19 digits
from_value(iso_node, dt) == ok, dt.epoch_ns() == 1791290096 * 1e9;      // ISO text
from_value(bad_node, dt) == invalid_number                              // "not-a-time"
from_value(bool_node, dt) == kind_mismatch

// write: explicit modes
to_value_dt(n, dt, date_time_output::numeric_seconds)  → integer 1791290096
to_value_dt(n, dt, date_time_output::numeric_ms)       → integer 1791290096123
to_value_dt(n, dt, date_time_output::numeric_us)       → integer 1791290096123456
to_value_dt(n, dt, date_time_output::numeric_ns)       → integer 1791290096123456789
to_value_dt(n, dt, date_time_output::iso_string)       → "2026-10-06T12:34:56.123456789Z"
```

---

## 4. `naming.hpp` — reflected-field key naming policy

> Extraction range: `include/rtti/naming.hpp` lines 1–53.
> Implementation: `src/naming.cpp` lines 1–69.

Format-agnostic key-naming knobs used by the optional format adapters
(JSON/XML/INI/TOML) when they serialize a reflected object through
`to_value_named` / parse it back through `from_value_named`
(`naming.hpp:1–15`). The transformations are pure functions and carry no
dependency on any format adapter.

### 4.1 `key_case`

```cpp
enum class key_case { identity, snake, camel };   // naming.hpp:26
```

- `identity` — field name as-is;
- `snake` — snake_case (`fieldName` → `field_name`);
- `camel` — camelCase (`first_name` → `firstName`).

### 4.2 `to_snake_case`

```cpp
std::string to_snake_case(rtti::string_view s);    // naming.hpp:31
```

- **Parameters** — `s`: reflected field name (e.g. `"fieldName"` /
  `"FieldName"`).
- **Returns** — the snake_case form. An uppercase run only gets an underscore
  before it when the **previous byte is a lowercase letter**, so already-snake
  names and acronyms stay unchanged (`src/naming.cpp:15–30`).

### 4.3 `to_camel_case`

```cpp
std::string to_camel_case(rtti::string_view s);    // naming.hpp:36
```

- **Parameters** — `s`: reflected field name (e.g. `"first_name"`).
- **Returns** — the camelCase form. Each underscore that is followed by a
  lowercase letter promotes that letter to uppercase and is dropped; other
  underscores and non-letter bytes are passed through
  (`src/naming.cpp:32–49`).

### 4.4 `naming_policy`

```cpp
struct naming_policy {                             // naming.hpp:41–45
    key_case kc = key_case::identity;
    const std::map<std::string, std::string>* rename_map = nullptr;
    bool skip_empty = false;
};
```

- `kc` — the casing policy applied when no rename applies.
- `rename_map` — optional **borrowed** pointer to a field-name → output-key
  override table; the map itself is owned by the caller (`nullptr` = none).
  A rename hit always wins over `key_case`.
- `skip_empty` — during serialization, drop fields whose value is empty
  (empty string, empty container, numeric zero, `false` bool, or null).

Aggregated by the format adapters' `write_config` from the per-adapter
`key_case` / `rename_map` / `skip_empty` fields (e.g. `json.hpp:60–75`
re-exports `rtti::detail::key_case` and mirrors the structure).

### 4.5 `apply_naming`

```cpp
std::string apply_naming(rtti::string_view field, const naming_policy& np);  // naming.hpp:50
```

- **Parameters** — `field`: reflected field name; `np`: the policy.
- **Returns** — the output key: a `rename_map` hit wins; otherwise the
  `key_case` transformation applies; `identity` returns the field name
  unchanged as a fresh `std::string` (`src/naming.cpp:51–66`).

**Behavior examples** (from the format-layer tests,
`tests/naming_config_test.cpp`):

```cpp
// key_case::snake  → fieldName serializes as "field_name"   (json, :61–66)
// key_case::camel  → first_name serializes as "firstName"   (json, :68–73)
// rename_map {"a"->"alpha"} wins over key_case             (json, :76–86)
// skip_empty drops empty string, empty vector, numeric zero  (:76–113)
// parse_as reverses the naming: output key -> field name    (:155–181)
// naming propagates recursively into nested structs/containers (:117–141)
```

---

## 5. `version.hpp` — library version

> Extraction range: `include/rtti/version.hpp` lines 1–6.

```cpp
#define RTTI_VERSION_MAJOR 0        // version.hpp:2
#define RTTI_VERSION_MINOR 1        // :3
#define RTTI_VERSION_PATCH 0        // :4
#define RTTI_VERSION_STRING "0.1.0" // :5
namespace rtti { inline const char* version() noexcept { return RTTI_VERSION_STRING; } }  // :6
```

- **`RTTI_VERSION_MAJOR` / `RTTI_VERSION_MINOR` / `RTTI_VERSION_PATCH`** —
  numeric version components.
- **`RTTI_VERSION_STRING`** — the `"major.minor.patch"` string literal.
- **`rtti::version()`** — returns the version string (never `nullptr`).
  Header-only (`inline`); identical in every translation unit.

### Single source of truth

`version.hpp` is a **generated** header. CMake embeds the project version into
the checked-in header at configure time:

```
project(RTTI VERSION 0.1.0 LANGUAGES CXX)            # CMakeLists.txt:2  ← the single source
configure_file(cmake/version.hpp.in
               include/rtti/version.hpp @ONLY)        # CMakeLists.txt:6  ← generator
```

The template `cmake/version.hpp.in:2–6` substitutes
`@PROJECT_VERSION_MAJOR@` / `@PROJECT_VERSION_MINOR@` /
`@PROJECT_VERSION_PATCH@` / `@PROJECT_VERSION@`. Bumping the version means
editing `project(RTTI VERSION …)`; the header and the package version file
(`write_basic_package_version_file`, `CMakeLists.txt:103–106`) stay in sync.

**Test** (`tests/type_support_test.cpp:38–39`):

```cpp
RTTI_TEST_CASE(VersionMacros) {
    RTTI_CHECK(std::string(rtti::version()) == std::string(RTTI_VERSION_STRING));
}
```

---

## 6. `rtti_fwd.hpp` — forward declarations & feature-gate defaults

> Extraction range: `include/rtti/rtti_fwd.hpp` lines 1–45.

`rtti_fwd.hpp` carries no implementation and no format-adapter include, so it
is safe to include from any translation unit.

### 6.1 `version_tag`

```cpp
struct version_tag {                 // rtti_fwd.hpp:9–13
    int major = 0;
    int minor = 1;
    int patch = 0;
};
```

Compile-time library version information. (Note: this struct carries hard-coded
defaults independent of the generated `version.hpp` macros.)
Tested in `tests/reflection_smoke.cpp:7–11`.

### 6.2 Forward declarations (defined in `reflection.hpp`)

```cpp
class attr_arg;                                  // :16   defined reflection.hpp:873
struct attr;                                     // :17   defined reflection.hpp:926
struct field_attr_entry;                         // :18   defined reflection.hpp:932
struct field_attr_list;                          // :19   defined reflection.hpp:938
template <typename T, typename> struct attr_table;     // :20   defined reflection.hpp:946
template <typename T, typename> struct field_attr_table; // :21  defined reflection.hpp:954
template <typename T, typename> struct template_info;   // :25   defined reflection.hpp:1721
```

These belong to the attribute system and the explicit template-argument
registry (spec 20); `RTTI_TEMPLATE_INFO` specializations supply the type list
and count.

### 6.3 Feature-gate macros — defaulted here

Each optional format adapter is gated by a macro that **defaults to 1** when
undefined, so a plain `#include <rtti/…>` enables everything:

```cpp
#if !defined(RTTI_ENABLE_JSON)  #define RTTI_ENABLE_JSON 1  #endif   // :29–31
#if !defined(RTTI_ENABLE_XML)   #define RTTI_ENABLE_XML  1  #endif   // :32–34
#if !defined(RTTI_ENABLE_INI)   #define RTTI_ENABLE_INI  1  #endif   // :35–37
#if !defined(RTTI_ENABLE_TOML)  #define RTTI_ENABLE_TOML 1  #endif   // :38–40
#if !defined(RTTI_ENABLE_PROTO) #define RTTI_ENABLE_PROTO 1 #endif   // :41–43
```

The reflect core itself compiles with every `RTTI_ENABLE_*` macro undefined
(`:28`).

### 6.4 Complete `RTTI_*` configuration table

| Macro | Default | Where default is defined | Meaning |
| --- | --- | --- | --- |
| `RTTI_ENABLE_JSON` | 1 | `rtti_fwd.hpp:30` | JSON format adapter |
| `RTTI_ENABLE_XML` | 1 | `rtti_fwd.hpp:33` | XML format adapter |
| `RTTI_ENABLE_INI` | 1 | `rtti_fwd.hpp:36` | INI format adapter |
| `RTTI_ENABLE_TOML` | 1 | `rtti_fwd.hpp:39` | TOML format adapter |
| `RTTI_ENABLE_PROTO` | 1 | `rtti_fwd.hpp:42` | protobuf format adapter |
| `RTTI_ENABLE_FLATBUFFERS` | 1 | `flat.hpp:86`, `flat_schema.hpp:73` (⚠ not in `rtti_fwd.hpp`) | FlatBuffers format adapter |
| `RTTI_ENABLE_FLAT` | ON | CMake option, `CMakeLists.txt:14` | CMake build switch mapped to the `RTTI_ENABLE_FLATBUFFERS`-gated sources |

Additional CMake build options (not format gates):
`RTTI_ENABLE_LTO` (default ON, `CMakeLists.txt:19`), and the option defaults for
`RTTI_ENABLE_JSON/XML/INI/TOML/PROTO/FLAT` (all ON, `CMakeLists.txt:9–14`)
which are passed to the compiler via `target_compile_definitions`
(`CMakeLists.txt:63–70`).

**Gating semantics.** The format-adapter headers `#error` out when their gate
evaluates to 0, e.g. `json.hpp:46–47` ("RTTI: JSON format adapter not enabled
(compile with -DRTTI_ENABLE_JSON=1)"). CMake then conditionally omits the
corresponding `src/*.cpp` from the library (`CMakeLists.txt:40–54`).
`tests/reflection_smoke.cpp:19–23` verifies the gate macros (when defined) are
0 or 1; `tests/naming_config_test.cpp:40–42` shows the typical
`#if (!defined(GATE) || GATE)` guard pattern consumers use.

---

## 7. Related documentation & editions

This reference covers only the tool/support cluster. Other consumers of these
headers: `include/rtti/convert.hpp` (bridges), `include/rtti/to_string.hpp`
(text formatting), and the four format adapters (`json.hpp`, `xml.hpp`,
`ini.hpp`, `toml.hpp`) which route naming through `naming_policy`.

简体中文版: [../zh/utils.md](../zh/utils.md).
Cross-references: [README.en.md](../../../README.en.md),
[README.zh-CN.md](../../../README.zh-CN.md).