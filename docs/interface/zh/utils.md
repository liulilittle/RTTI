# RTTI — 工具/支持小型头簇:接口参考

> 版次:简体中文。English edition: [../en/utils.md](../en/utils.md)。
>
> **准确性契约。** 下文每个签名均逐字摘自头文件。每章标注其提取行范围。
> 示例值全部取自仓库自身测试断言(`tests/base_convert_test.cpp`、
> `tests/date_time_test.cpp`、`tests/naming_config_test.cpp`、
> `tests/type_support_test.cpp`)——不新编任何数值。声明的实现位置交叉引用到
> `src/` 对应编译单元。

**覆盖的头文件**

| 头文件 | 主题范围 | 提取行范围 |
| --- | --- | --- |
| `include/rtti/base_convert.hpp` | 任意进制整数 ↔ 字符串 | 第 1–112 行 |
| `include/rtti/date_time.hpp` | Unix 纪元纳秒时间戳值类型 | 第 1–247 行 |
| `include/rtti/naming.hpp` | 反射字段键命名策略 | 第 1–53 行 |
| `include/rtti/version.hpp` | 版本宏 + `rtti::version()` | 第 1–6 行 |
| `include/rtti/rtti_fwd.hpp` | 前置声明 + 门控宏默认值 | 第 1–45 行 |

定义在 `include/rtti/convert.hpp` 中的转换桥入口(`to_value_dt`、
`date_time_output`、`to_value_base` / `from_value_base`)按「其所桥接的值类型」
归入对应章节,因为这才是 `date_time` / `base_convert` 调用方真正触碰的接口面。

---

## 1. 全局入口与构建模型

### 1.1 如何 include

所有公开头文件以 `include/` 为根包含:

```cpp
#include <rtti/base_convert.hpp>   // rtti::detail::to_base / from_base …
#include <rtti/date_time.hpp>      // rtti::date_time、rtti::epoch_ns_from_digits
#include <rtti/naming.hpp>         // rtti::detail::key_case / naming_policy …
#include <rtti/version.hpp>        // RTTI_VERSION_* + rtti::version()
#include <rtti/rtti_fwd.hpp>       // version_tag + RTTI_ENABLE_* 默认值
```

公开 API 在 `rtti` 命名空间;转换/命名助手在 `rtti::detail`,按实际位置记载。

### 1.2 是否为 header-only —— 事实说明

**本簇不是 header-only。** 范围内的头文件声明了一批函数,其定义编译进静态库:

| 头文件 | `src/` 中的实现单元 |
| --- | --- |
| `base_convert.hpp` | `src/base_convert.cpp` |
| `date_time.hpp` | `src/date_time.cpp` |
| `naming.hpp` | `src/naming.cpp` |

`README.md`(第 24–37 行)写明 `src/` 为 "static-library sources":即便所有格式
门控全关,reflect 核心仍链接静态库。CMake 目标为 `rtti`
(`add_library(rtti STATIC …)`,`CMakeLists.txt:22`),对外导出为 `rtti::rtti`,
带 `BUILD_INTERFACE`/`INSTALL_INTERFACE` 包含目录(`CMakeLists.txt:58–60、97`)。
使用方必须链接该库,然后即可 `#include <rtti/…>`。

真正在头文件内联的例外:

- `rtti::version()` —— `inline`,定义于 `version.hpp:6`;
- `rtti::epoch_ns_from_digits` —— `inline`,定义于 `date_time.hpp:192–245`;
- `date_time` 的比较/算术友元运算符与 `is_date_time` 特征 —— 定义于
  `date_time.hpp:105–186`。

### 1.3 通用约定(spec §0)

- 失败一律通过 `rtti::error_code` / `rtti::result<T>` 上报;无法处理的输入产生
  错误码且**绝不崩溃、绝不静默回绕**。唯一文档化例外是 `to_base`——它没有错误
  通道,改用空字符串表达失败(见 §2.3)。
- 本簇涉及的 `rtti::error_code` 取值:`ok`、`invalid_number`、`overflow`、
  `unexpected_token`、`kind_mismatch`、`no_such_field`(`error.hpp:14–39`)。
- `rtti::string_view` 即 `std::string_view` 的别名(`memory.hpp:18`)。
- C++17;无第三方依赖。

---

## 2. `base_convert.hpp` —— 任意进制整数 ↔ 字符串转换

> 提取行范围:`include/rtti/base_convert.hpp` 第 1–112 行。
> 实现:`src/base_convert.cpp` 第 1–206 行。

把 `uint64_t` 与任意 **2..64** 进制的数字字符串互转(spec §15)。
`to_base_signed` / `from_base_signed` 为 `int64_t` 增加前导 `'-'` 号。
数字表可配置;溢出 / 非法字符 / 非法进制均返回 `error_code`,绝不崩溃。

### 2.1 数字表

默认表(spec §15.2)—— `src/base_convert.cpp:19–23`:

```
"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+/"
```

下标 `i` 即数值 `i` 的符号,故前 `base` 个符号构成该进制的字母表:

- 进制 2..36 → `0-9a-z`(与 strtol 兼容的小写集);
- 进制 37..62 → `0-9a-zA-Z`(62 个符号);
- 进制 63..64 → 62 个符号之外下标 62 为 `'+'`、下标 63 为 `'/'`。

RFC 4648 base64 顺序可直接取用(`src/base_convert.cpp:25–29`):

```
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
```

#### `const char* default_digits() noexcept;` —— `base_convert.hpp:47`

返回上述默认表(稳定指针,绝不返回 `nullptr`)。

#### `const char* rfc4648_digits() noexcept;` —— `base_convert.hpp:52`

返回 RFC 4648 base64 顺序。把它显式作为任意函数的 `digits` 参数传入,即可
对 64 以内的任何进制选用该顺序。

#### `std::size_t digit_table_len(const char* digits) noexcept;` —— `base_convert.hpp:55`

表内符号个数;`digits` 为 `nullptr` 时返回 `0`。

#### `bool default_case_insensitive(unsigned base) noexcept;` —— `base_convert.hpp:61`

`base <= 36` 时为 `true`。即 strtol 兼容规则:对 2..36 进制,默认小写表按
大小写不敏感匹配字母(`"FF"` 解析为 255)。自定义表默认严格大小写敏感;调用方
显式选择不敏感(见 5 参 `from_base`)。

#### `int base_digit(char c, const char* digits, std::size_t base, bool insensitive) noexcept;` —— `base_convert.hpp:67`

字节 `c` 在前 `base` 个 `digits` 符号内的数字值,不在其中则返回 `-1`。置
`insensitive` 时,无精确匹配的字节会以相反 ASCII 大小写重试(`'A'` 可在纯小写
表命中,反之亦然)。实现 `src/base_convert.cpp:42–63`。

### 2.2 大小写策略(汇总)

- 进制 ≤ 36、默认表:**大小写不敏感**——`from_base("FF", 16, out)` == 255。
- 进制 > 36:**大小写敏感**——`'a'` 与 `'A'` 是不同数字(`'a'`=10、`'A'`=36,
  进制 62)。
- 自定义表:默认敏感;传入显式 `case_insensitive` 标志才启用大小写回退。

### 2.3 `to_base` / `to_base_signed` —— 整数 → 字符串

```cpp
std::string to_base(std::uint64_t value, unsigned base,
                    const char* digits = nullptr);        // base_convert.hpp:74
std::string to_base_signed(std::int64_t value, unsigned base); // base_convert.hpp:79
```

- **参数** —— `value`:待转换整数;`base`:2..64;`digits`:可选符号表,须至少含
  `base` 个符号(缺省用 `default_digits()`)。
- **返回** —— 新分配的 `std::string`。**失败信号:** 非法进制(0、1、>64)或
  表过短返回**空字符串**(本函数无 `error_code` 通道)。零转成单个 `"0"`。
  `to_base_signed` 给负值加 `'-'` 前缀,非法进制同样传播空串。
- **说明** —— `INT64_MIN` 的模(2^63)用补码模手法处理,可精确往返
  (`src/base_convert.cpp:94–108`)。

**示例**(逐字取自 `tests/base_convert_test.cpp:40–56、107–118`):

```cpp
bc::to_base(0u, 2)   == "0"
bc::to_base(0u, 64)  == "0"
bc::to_base(255u, 16) == "ff"
bc::to_base(255u, 8)  == "377"
bc::to_base(255u, 2)  == "11111111"
bc::to_base(255u, 36) == "73"
bc::to_base(255u, 64) == "3/"     // '/' 是第 64 个符号
bc::to_base(255u, 63) == "43"
bc::to_base(42u, 62)  == "G"
bc::to_base(4095u, 36) == "35r"
bc::to_base(46655u, 36) == "zzz"
bc::to_base(3843u, 62) == "ZZ"    // 进制 62 中 'A'(36) 与 'a'(10) 不同

bc::to_base_signed(-255, 16)                         == "-ff"
bc::to_base_signed(INT64_MIN, 10)                    == "-9223372036854775808"
bc::to_base_signed(INT64_MAX, 10)                    == "9223372036854775807"
bc::to_base_signed(INT64_MIN, 2)                     == "-1" + std::string(63, '0')
```

输出侧非法进制(`base_convert_test.cpp:201–204`);表过短(`:236`):

```cpp
bc::to_base(255u, 0)  == ""      // 非法进制
bc::to_base(255u, 1)  == ""
bc::to_base(255u, 65) == ""
bc::to_base(255u, 70) == ""
bc::to_base(255u, 32, kBaseConvertShort) == ""   // 表短于进制
```

### 2.4 `from_base` —— 字符串 → 无符号整数(4 个重载)

```cpp
error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out, const char* digits,
                     bool case_insensitive);                // base_convert.hpp:87  (主实现)
error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out);                   // base_convert.hpp:92  (默认表,strtol 大小写策略)
error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out, bool case_insensitive); // base_convert.hpp:96 (默认表,显式标志)
error_code from_base(std::string_view s, unsigned base,
                     std::uint64_t& out, const char* digits);    // base_convert.hpp:100 (自定义表,大小写敏感)
```

- **参数** —— `s`:数字字符串;`base`:2..64;`out`:结果;`digits`:可选表(默认
  `default_digits()`);`case_insensitive`:启用 `base_digit` 的大小写回退。
- **返回** —— 成功:`error_code::ok` 且写入 `out`。失败:
  - `invalid_number` —— 非法进制(0、1、>64)、空输入、字符不在该进制数字集内、
    自定义表过短;
  - `overflow` —— 数值将超过 `UINT64_MAX`。
  累加器在**乘法之前**校验(`acc > (UINT64_MAX - d) / base`),因此溢出车道被
  检出而非回绕(`src/base_convert.cpp:126–140`)。
- **重载语义** —— 3 参形式用默认表 + strtol 大小写策略
  (`default_case_insensitive(base)`)。4 参 `bool` 形式沿用默认表并置标志。
  4 参 `const char*` 形式用自定义表、大小写敏感。主实现两者皆显式。

**示例** —— 已知值与大小写(`base_convert_test.cpp:244–249`):

```cpp
from_base("ff", 16, out) == ok, out == 255
from_base("FF", 16, out) == ok, out == 255     // base ≤ 36,strtol 大小写不敏感
from_base("DeAdBeEf", 16, out) == ok, out == 0xDEADBEEF
```

进制 > 36 大小写敏感(`:251–254`):

```cpp
from_base("a", 62, out) == ok, out == 10
from_base("A", 62, out) == ok, out == 36
```

溢出(绝不回绕)—— `:146–168`:

```cpp
from_base("18446744073709551616", 10, out) == overflow    // 恰为 2^64
from_base("18446744073709551615", 10, out) == ok, out == UINT64_MAX
from_base(std::string(64, '1'), 2, out) == ok             // 64 位全容纳
from_base(std::string(65, '1'), 2, out) == overflow
from_base(std::string(16, 'f'), 16, out) == ok            // 2^64 - 1
from_base(std::string(17, 'f'), 16, out) == overflow
```

非法输入 —— `:181–199`:

```cpp
from_base("10", 0, out)  == invalid_number   // 进制须为 2..64
from_base("10", 1, out)  == invalid_number
from_base("10", 65, out) == invalid_number
from_base("", 10, out)   == invalid_number   // 空输入
from_base("1g", 16, out) == invalid_number   // 'g' 不是十六进制数字
from_base("1a", 10, out) == invalid_number
from_base("102", 2, out) == invalid_number
from_base("3/", 63, out) == invalid_number   // '/' 是第 64 个符号,进制 63 越界
from_base("z", 35, out)  == invalid_number   // 进制 35 止于 'y'
from_base(" ", 10, out)  == invalid_number
from_base("!", 64, out)  == invalid_number
```

### 2.5 `from_base_signed` —— 字符串 → 有符号整数

```cpp
error_code from_base_signed(std::string_view s, unsigned base,
                            std::int64_t& out);            // base_convert.hpp:108
```

- **参数** —— `s`:带符号数字串;`base`:2..64;`out`:结果。
- **返回** —— 成功 `error_code::ok` 写入 `out`;`invalid_number` 对应非法进制、
  空输入、裸/双/错位符号(`"-"`、`"+"`、`"--1"`、错位的 `"1-"`)或非法字符;
  `overflow` 当模超过 `INT64_MAX`(正)或 2^63(负)。
- **说明** —— 接受可选 `'+'`/`'-'` 前缀(strtol 风格);模经 `from_base` 按默认
  表 + strtol 大小写策略解析。`INT64_MIN` 经其 2^63 模精确可表示
  (`src/base_convert.cpp:157–203`)。

**示例**(`base_convert_test.cpp:121–128、170–178、208–219`):

```cpp
from_base_signed("-9223372036854775808", 10, back) == ok, back == INT64_MIN
from_base_signed("9223372036854775807", 10, back) == ok, back == INT64_MAX
from_base_signed("-1", 2, back)                    == ok, back == -1

from_base_signed("9223372036854775808", 10, so)   == overflow   // INT64_MAX + 1
from_base_signed("-9223372036854775809", 10, so)  == overflow   // -(2^63) - 1

from_base_signed("-", 10, so)  == invalid_number   // 裸符号
from_base_signed("+", 10, so)  == invalid_number
from_base_signed("--1", 10, so) == invalid_number  // 双符号
from_base_signed("1-", 10, so) == invalid_number   // 符号错位
from_base_signed("", 10, so)   == invalid_number
```

### 2.6 自定义数字表

传入任意至少含 `base` 个符号的 `digits` 表。URL-safe base64 符号
(`base_convert_test.cpp:33–38、222–239`):

```cpp
static const char kUrlSafe[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_";

to_base(255u, 64, kUrlSafe)       == "3_"
to_base(62u, 64, kUrlSafe)        == "-"
to_base(63u, 64, kUrlSafe)        == "_"
to_base(4095u, 64, kUrlSafe)      == "__"
from_base("3_", 64, back, kUrlSafe) == ok, back == 255
```

表短于进制时**两侧**都拒绝——`to_base` 返回 `""`,`from_base` 返回
`invalid_number`(`base_convert_test.cpp:236–238`)。

自定义表的大小写处理(`base_convert_test.cpp:257–264`,纯大写表):

```cpp
from_base("FF", 36, out, kUpper)       == ok, out == 555
from_base("ff", 36, out, kUpper)       == invalid_number       // 默认敏感
from_base("ff", 36, out, kUpper, true) == ok, out == 555       // 显式标志
```

### 2.7 转换桥(定义于 `convert.hpp`,spec §15.4)

base-N 整数车道的值树序列化桥接声明于 `include/rtti/convert.hpp:572–620`
(inline):

```cpp
error_code to_value_base(value_io* dst, std::uint64_t value, unsigned base, const char* digits = nullptr);
error_code from_value_base(const value_io* src, unsigned base, std::uint64_t& out);
error_code to_value_base_signed(value_io* dst, std::int64_t value, unsigned base);
error_code from_value_base_signed(const value_io* src, unsigned base, std::int64_t& out);
```

`to_value_*` 把 base-N 字符串写为文本标量(进制非法返回 `invalid_number`);
`from_value_*` 读回文本标量(非文本节点返回 `kind_mismatch`)。两者都是上述
函数的薄封装。

---

## 3. `date_time.hpp` —— Unix 纪元纳秒时间戳

> 提取行范围:`include/rtti/date_time.hpp` 第 1–247 行。
> 实现:`src/date_time.cpp` 第 1–398 行。

`rtti::date_time` 是一个值类型,以单个 `int64_t` 保存 **Unix 纪元纳秒**,带
ISO 8601 解析/格式化与精度感知的单位换算(spec §14)。

核心设计(头注释,`date_time.hpp:5–19`):

- `int64` 纳秒存储按 ns 分辨率覆盖约 **1677..2262 年**;
- `from_unix_*` 对换算乘积做范围校验,越界报 `error_code::overflow`,绝不回绕
  (spec §0);
- `parse()` 接受 ISO 8601 子集 `YYYY-MM-DDTHH:MM:SS[.frac][Z|+/-HH:MM]`
  (小数 1..9 位,偏移归一化到 UTC),畸形文本返回 `error_code::invalid_number`;
- `format()` / `to_string()` 输出规范 UTC 形式,精确往返;
- `format(string_view pattern)` 输出 strftime 风格子集(spec §7.1);未知或畸形
  指令报 `error_code::unexpected_token`(字符串重载返回空串)——failsafe,
  绝不崩溃;
- 数字纪元 → ns 按十进制位数推断(10 位 = 秒、13 = 毫秒、16 = 微秒、19/20 =
  纳秒),见 `epoch_ns_from_digits`。

### 3.1 构造

```cpp
date_time() noexcept;                      // date_time.hpp:37  —— 纪元零点(1970-01-01T00:00:00Z)
explicit date_time(std::int64_t epoch_ns) noexcept;  // date_time.hpp:39 —— 精确纳秒数
```

```cpp
static result<date_time> from_unix_seconds(std::int64_t seconds); // :43
static result<date_time> from_unix_ms(std::int64_t ms);           // :44
static result<date_time> from_unix_us(std::int64_t us);           // :45
static result<date_time> from_unix_ns(std::int64_t ns);           // :46
```

- **参数** —— 以指定单位表示的 Unix 纪元。
- **返回** —— `result<date_time>`;换算出的纳秒乘积被范围校验,乘积离开
  `int64` 纳秒车道时报 `error_code::overflow`(`src/date_time.cpp:21–44`)。
- **边界**(来自实现):

| 工厂函数 | 可接受输入范围 | 换算后 ns |
| --- | --- | --- |
| `from_unix_seconds` | ±9'223'372'036 | × 10^9 |
| `from_unix_ms` | ±9'223'372'036'854 | × 10^6 |
| `from_unix_us` | ±9'223'372'036'854'775 | × 10^3 |
| `from_unix_ns` | 任意 `int64_t` | 原样 |

**示例**(`date_time_test.cpp:41–94`):

```cpp
auto s = date_time::from_unix_seconds(1791290096);
s.ok() && s.value.to_unix_ms(ms) == ok && ms == 1791290096000;
s.value.to_unix_ns(ns) == ok && ns == 1791290096 * 1000000000LL;

from_unix_seconds(9223372036).ok()  == true     // 边界,ok
from_unix_seconds(9223372037).err.code == overflow
from_unix_ms(9223372036855).err.code     == overflow
from_unix_us(9223372036854776).err.code  == overflow
from_unix_us(9223372036854775).ok()      == true
from_unix_seconds(-9223372037).err.code  == overflow
from_unix_ns(INT64_MAX).ok()             == true
```

### 3.2 单位访问器

```cpp
error_code to_unix_seconds(std::int64_t& out) const noexcept; // :52
error_code to_unix_ms(std::int64_t& out) const noexcept;      // :53
error_code to_unix_us(std::int64_t& out) const noexcept;      // :54
error_code to_unix_ns(std::int64_t& out) const noexcept;      // :55
std::int64_t epoch_ns() const noexcept;                       // :58
```

- **参数** —— `out`:接收指定单位的时刻。
- **返回** —— 恒为 `error_code::ok`;因除数至少为 1000,这些折算都不可能在
  `int64` 输出上溢出。
- **说明** —— 亚秒计数使用**向负无穷取整的除法(floor division)**,因此带小数
  的纪元前时刻映射到实际包含它的那个整秒。例如
  `date_time(INT64_MIN).to_unix_seconds()` == -9'223'372'037(比截断的
  -9'223'372'036 再低一秒)(`date_time_test.cpp:246–254`)。
- `epoch_ns()` 返回裸纳秒数。

### 3.3 规范 ISO 8601 文本

```cpp
std::string format() const;      // :63   规范 UTC 形式
std::string to_string() const;   // :65   format() 的别名
```

- **说明** —— 整秒输出为 `YYYY-MM-DDTHH:MM:SSZ`;非零亚秒部分追加能精确表示该
  值的最短 `".fff"` / `".ffffff"` / `".fffffffff"` 小数。与 `parse()` 精确往返。

**示例**(`date_time_test.cpp:100–154、238–254`):

```cpp
date_time().format() == "1970-01-01T00:00:00Z"
parse("2026-10-06T12:34:56Z").value.format()        == "2026-10-06T12:34:56Z"
parse("2026-10-06T12:34:56.123Z").value.format()    == "2026-10-06T12:34:56.123Z"
parse("2026-10-06T12:34:56.123456Z").value.format() == "2026-10-06T12:34:56.123456Z"
parse("2026-10-06T12:34:56.123456789Z").value.format() == "2026-10-06T12:34:56.123456789Z"
date_time(INT64_MIN).format() 可解析回 INT64_MIN(floor 秒 == -9223372037)
```

### 3.4 strftime 风格模式格式化

```cpp
error_code format(string_view pattern, std::string& out) const;  // :91
std::string format(string_view pattern) const;                   // :95
```

- **参数** —— `pattern`:格式串;`out`(出参重载):目标缓冲。
- **返回** —— 出参重载:`error_code::ok`,或未知指令、孤立尾部 `'%'`、任何畸形
  模式时的 `error_code::unexpected_token`;出错时 `out` 保留失败前已产生的部分
  文本(该重载**追加**、绝不清空)。字符串重载:格式化文本,任何不支持的指令
  返回空串。
- **支持的指令**(`date_time.hpp:68–81`):

| 指令 | 输出 |
| --- | --- |
| `%Y` | 十进制年份,至少四位 |
| `%y` | 年份 mod 100,两位 |
| `%m` | 月 01..12 |
| `%d` | 日 01..31 |
| `%H` | 时 00..23 |
| `%M` | 分 00..59 |
| `%S` | 秒 00..59 |
| `%f` | 纳秒小数,恰九位 |
| `%F` | `%Y-%m-%d` |
| `%T` | `%H:%M:%S` |
| `%R` | `%H:%M` |
| `%z` | UTC 偏移,`"+0000"` |
| `%Z` | 时区名,`"UTC"` |
| `%%` | 字面 `'%'` |

其余字节原样复制。时刻总是 UTC 归一化(偏移永不随解析存活),故 `%z` 与
`%Z` 是常量。

### 3.5 `parse`

```cpp
static result<date_time> parse(string_view iso);    // :102
```

- **参数** —— `iso`:ISO 8601 子集 `YYYY-MM-DDTHH:MM:SS[.frac][Z | +/-HH:MM]`。
- **返回** —— `result<date_time>`:
  - `error_code::ok` 附带解析后的时刻;
  - `invalid_number`:任何畸形文本(绝不抛出);
  - `overflow`:结果时刻离开 int64 ns 范围。
- **说明**(`src/date_time.cpp:186–282`)—— 小数位 1..9 位,原样转为纳秒(10+ 位
  拒绝)。时区指示符:`Z` 或 `z` 表 UTC,`+/-HH:MM` 表偏移(时 00..23、分
  00..59),归一化到 UTC。历法规则被遵循(闰年、月长、小时 00..23)。整秒车道须
  落在扩展界 `-9223372037 .. +9223372036` 秒内(多出的一个负秒即
  `INT64_MIN / 1e9` 的 floor),并有精确的亚秒边界检查。

**示例**(`date_time_test.cpp:96–160、205–235`):

```cpp
parse("2026-10-06T12:34:56Z").value.epoch_ns()        == 1791290096 * 1e9
parse("2026-10-06T20:34:56+08:00").value.epoch_ns()   == 同一时刻  // 偏移 → UTC
parse("2026-10-06T04:34:56-08:00").value.epoch_ns()   == 同一时刻
parse("2026-10-06T12:34:56+00:00").value.epoch_ns()   == 同一时刻
parse("2026-10-06T20:34:56.5+08:00").value.epoch_ns() == 1791290096500000000
parse("1970-01-01T00:00:00Z").value.epoch_ns()        == 0
parse("1969-12-31T23:59:59Z").value.epoch_ns()        == -1000000000
parse("1969-12-31T23:59:58.5Z").value.epoch_ns()      == -1500000000   // floor 除法
parse("2024-02-29T00:00:00Z").ok()                    == true          // 闰年

parse("9999-12-31T23:59:59Z").err.code == overflow       // 超出 int64 ns 范围
parse("2026-10-06").ok()        == false                 // 畸形
parse("2026-13-01T00:00:00Z")    == invalid_number       // 13 月
parse("2023-02-29T00:00:00Z")    == invalid_number       // 非闰年
parse("2026-10-06T12:34:56")     == invalid_number       // 缺时区指示
parse("2026-10-06T12:34:56Zextra") == invalid_number     // 尾随垃圾
parse("2026-10-06T12:34:56.1234567890Z") == invalid_number  // 10 位小数
parse("2026-10-06T12:34:56+25:00")     == invalid_number // 偏移小时越界
```

### 3.6 比较与算术

```cpp
friend bool operator==/!=/</<=/>/>= (const date_time& a, const date_time& b) noexcept; // :105–122 (纳秒精确)
friend date_time operator+(date_time t, std::int64_t ns) noexcept;  // :128
friend date_time operator-(date_time t, std::int64_t ns) noexcept;  // :133
friend std::int64_t operator-(const date_time& a, const date_time& b) noexcept; // :139
```

- 比较运算符按纳秒计数精确比较。
- 算术按无符号计数执行,因此越界操作数确定性地回绕(定义行为,绝无有符号溢出);
  需要严格范围语义的调用方应走受检的 `from_unix_*` / `epoch_ns_from_digits`
  路径(`date_time.hpp:124–143`)。

**示例**(`date_time_test.cpp:257–276`):

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

仅对 `rtti::date_time` 为 `true`。转换分发借此把 `date_time` 路由到标量转换器,
即便用户同时用 `RTTI_DEFINE_TYPE` 注册了该类型。

### 3.8 `epoch_ns_from_digits` —— 按位数推断单位

```cpp
inline error_code epoch_ns_from_digits(std::int64_t value,
                                       std::int64_t& out) noexcept;  // :192–193
```

- **参数** —— `value`:整数 Unix 纪元;`out`:纳秒。
- **返回** —— `error_code::ok` 并写 `out`,或:
  - `invalid_number` —— 位数不是 10/13/16/19/20 之一(前导符号不计数;零映射
    纪元零点,按秒);
  - `overflow` —— `value * factor` 将离开 int64 ns 范围。
- **单位表**(实现,`date_time.hpp:212–239`):

| 位数 | 因子 | 合法 `value` 窗口 |
| --- | --- | --- |
| 10 | 1'000'000'000 | −9'223'372'036 … +9'223'372'036 |
| 13 | 1'000'000 | −9'223'372'036'854 … +9'223'372'036'854 |
| 16 | 1'000 | −9'223'372'036'854'775 … +9'223'372'036'854'775 |
| 19 | 1 | `INT64_MIN` … `INT64_MAX` |
| 20 | 1 | `INT64_MIN` … `INT64_MAX` |

**ns 车道横跨整个 int64 范围** —— `INT64_MIN` 与 `INT64_MAX` 都是合法的
19 位纳秒值。10/13/16 位车道落在 `value * factor` 不可能离开 int64 的对称
±bound 窗口内。

**示例**(`date_time_test.cpp:162–203`):

```cpp
epoch_ns_from_digits(1789263451, ns)        == ok, ns == 1789263451000000000        // 10 位 → 秒
epoch_ns_from_digits(1789263451012, ns)     == ok, ns == 1789263451012000000        // 13 位 → 毫秒
epoch_ns_from_digits(1789263451012345, ns)  == ok, ns == 1789263451012345000        // 16 位 → 微秒
epoch_ns_from_digits(1789263451012345678, ns) == ok, ns == 1789263451012345678      // 19 位 → 纳秒
epoch_ns_from_digits(-1789263451, ns)       == ok, ns == -1789263451000000000       // 符号不计数
epoch_ns_from_digits(0, ns)                 == ok, ns == 0                          // 零 → 纪元零点
epoch_ns_from_digits(INT64_MIN, ns)         == ok, ns == INT64_MIN                  // 完整 ns 车道
epoch_ns_from_digits(INT64_MAX, ns)         == ok, ns == INT64_MAX

epoch_ns_from_digits(123456789, ns)    == invalid_number   // 9 位
epoch_ns_from_digits(123456789012, ns) == invalid_number   // 12 位
epoch_ns_from_digits(1, ns)            == invalid_number
epoch_ns_from_digits(9223372037, ns)   == overflow         // 超出 10 位峰值一位
epoch_ns_from_digits(9999999999999, ns) == overflow        // 13 位车道溢出
```

### 3.9 转换桥(定义于 `convert.hpp`)

`date_time` 经 `value_io` 的读取路径是 `converter<date_time>` 特化
(`convert.hpp:447–483`):

- **读**(`from_value(src, dt)`):整数标量的单位经 `epoch_ns_from_digits` 推断;
  ISO 文本标量经 `parse()` 解析;其它节点形态 → `kind_mismatch`;畸形文本 →
  `invalid_number`。
- **写**(`to_value(dst, dt)`):默认输出数值纪元**秒**。

显式输出模式:

```cpp
enum class date_time_output { numeric_seconds, numeric_ms, numeric_us, numeric_ns, iso_string };  // convert.hpp:439–445

inline error_code to_value_dt(value_io* dst, const date_time& in,
                              date_time_output mode);          // convert.hpp:490–491
```

- `to_value_dt` 按选定单位写标量;`iso_string` 输出规范 `format()` 文本(精确
  保留亚秒精度)。

> **注意(与任务清单的差异)。** `rtti::from_value_dt` **在代码库中不存在**。
> 节点 → `date_time` 的读取路径是上面的通用 `from_value` /
> `converter<date_time>`,自动推断单位。只有 `to_value_dt` 是专用入口。

- **反射成员**:`date_time` 成员经 ISO 字符串车道序列化
  (`member_to_value`,`convert.hpp:543–561`),使亚秒精度在反射结构体往返中存活。

**示例**(`date_time_test.cpp:278–378`):

```cpp
// 读:数值标量自动推断单位
from_value(sec_node, dt) == ok, dt.epoch_ns() == 1791290096 * 1e9;      // 10 位
from_value(ms_node,  dt) == ok, dt.epoch_ns() == 1791290096123000000;   // 13 位
from_value(ns_node,  dt) == ok, dt.epoch_ns() == 1791290096123456789;   // 19 位
from_value(iso_node, dt) == ok, dt.epoch_ns() == 1791290096 * 1e9;      // ISO 文本
from_value(bad_node, dt) == invalid_number                              // "not-a-time"
from_value(bool_node, dt) == kind_mismatch

// 写:显式模式
to_value_dt(n, dt, date_time_output::numeric_seconds)  → 整数 1791290096
to_value_dt(n, dt, date_time_output::numeric_ms)       → 整数 1791290096123
to_value_dt(n, dt, date_time_output::numeric_us)       → 整数 1791290096123456
to_value_dt(n, dt, date_time_output::numeric_ns)       → 整数 1791290096123456789
to_value_dt(n, dt, date_time_output::iso_string)       → "2026-10-06T12:34:56.123456789Z"
```

---

## 4. `naming.hpp` —— 反射字段键命名策略

> 提取行范围:`include/rtti/naming.hpp` 第 1–53 行。
> 实现:`src/naming.cpp` 第 1–69 行。

与格式无关的键命名旋钮,供可选格式适配器(JSON/XML/INI/TOML)经
`to_value_named` 序列化、`from_value_named` 解析反射对象时使用
(`naming.hpp:1–15`)。这些转换是纯函数,不依赖任何格式适配器。

### 4.1 `key_case`

```cpp
enum class key_case { identity, snake, camel };   // naming.hpp:26
```

- `identity` —— 字段名原样;
- `snake` —— snake_case(`fieldName` → `field_name`);
- `camel` —— camelCase(`first_name` → `firstName`)。

### 4.2 `to_snake_case`

```cpp
std::string to_snake_case(rtti::string_view s);    // naming.hpp:31
```

- **参数** —— `s`:反射字段名(如 `"fieldName"` / `"FieldName"`)。
- **返回** —— snake_case 形式。仅有当大写段**前一字节是小写字母**时,才在其前
  加下划线,故已是 snake 的名字和缩写保持不变(`src/naming.cpp:15–30`)。

### 4.3 `to_camel_case`

```cpp
std::string to_camel_case(rtti::string_view s);    // naming.hpp:36
```

- **参数** —— `s`:反射字段名(如 `"first_name"`)。
- **返回** —— camelCase 形式。每个后随小写字母的下划线将该字母升为大写并删除;
  其余下划线与非字母字节原样通过(`src/naming.cpp:32–49`)。

### 4.4 `naming_policy`

```cpp
struct naming_policy {                             // naming.hpp:41–45
    key_case kc = key_case::identity;
    const std::map<std::string, std::string>* rename_map = nullptr;
    bool skip_empty = false;
};
```

- `kc` —— 无 rename 命中时应用的命名风格策略。
- `rename_map` —— 可选的**借用**指针,指向 字段名 → 输出键 的覆盖表;表本身由
  调用方持有(`nullptr` = 无)。rename 命中恒优先于 `key_case`。
- `skip_empty` —— 序列化时丢弃值为空的字段(空字符串、空容器、数值零、`false`
  布尔或 null)。

由格式适配器的 `write_config` 从各自的 `key_case` / `rename_map` /
`skip_empty` 字段聚合而来(如 `json.hpp:60–75` 再导出 `rtti::detail::key_case`
并镜像该结构)。

### 4.5 `apply_naming`

```cpp
std::string apply_naming(rtti::string_view field, const naming_policy& np);  // naming.hpp:50
```

- **参数** —— `field`:反射字段名;`np`:策略。
- **返回** —— 输出键:`rename_map` 命中优先;否则应用 `key_case` 变换;
  `identity` 返回字段名原样的新 `std::string`(`src/naming.cpp:51–66`)。

**行为示例**(来自格式层测试 `tests/naming_config_test.cpp`):

```cpp
// key_case::snake  → fieldName 序列化为 "field_name"   (json, :61–66)
// key_case::camel  → first_name 序列化为 "firstName"   (json, :68–73)
// rename_map {"a"->"alpha"} 优先于 key_case            (json, :76–86)
// skip_empty 丢弃空串、空 vector、数值零                 (:76–113)
// parse_as 反向应用命名:输出键 -> 字段名               (:155–181)
// 命名递归传播进嵌套结构体/容器                          (:117–141)
```

---

## 5. `version.hpp` —— 库版本

> 提取行范围:`include/rtti/version.hpp` 第 1–6 行。

```cpp
#define RTTI_VERSION_MAJOR 0        // version.hpp:2
#define RTTI_VERSION_MINOR 1        // :3
#define RTTI_VERSION_PATCH 0        // :4
#define RTTI_VERSION_STRING "0.1.0" // :5
namespace rtti { inline const char* version() noexcept { return RTTI_VERSION_STRING; } }  // :6
```

- **`RTTI_VERSION_MAJOR` / `RTTI_VERSION_MINOR` / `RTTI_VERSION_PATCH`** ——
  数值版本分量。
- **`RTTI_VERSION_STRING`** —— `"major.minor.patch"` 字符串字面量。
- **`rtti::version()`** —— 返回版本字符串(绝不返回 `nullptr`)。头文件内联
  (`inline`);所有翻译单元一致。

### 单一来源

`version.hpp` 是**生成**头。CMake 在配置期把工程版本嵌入检入的头文件:

```
project(RTTI VERSION 0.1.0 LANGUAGES CXX)            # CMakeLists.txt:2  ← 单一来源
configure_file(cmake/version.hpp.in
               include/rtti/version.hpp @ONLY)        # CMakeLists.txt:6  ← 生成器
```

模板 `cmake/version.hpp.in:1–6` 替换 `@PROJECT_VERSION_MAJOR@` /
`@PROJECT_VERSION_MINOR@` / `@PROJECT_VERSION_PATCH@` / `@PROJECT_VERSION@`。
升级版本只需改 `project(RTTI VERSION …)`;头文件与包版本文件
(`write_basic_package_version_file`,`CMakeLists.txt:103–106`)随之保持同步。

**测试**(`tests/type_support_test.cpp:38–39`):

```cpp
RTTI_TEST_CASE(VersionMacros) {
    RTTI_CHECK(std::string(rtti::version()) == std::string(RTTI_VERSION_STRING));
}
```

---

## 6. `rtti_fwd.hpp` —— 前置声明与门控默认值

> 提取行范围:`include/rtti/rtti_fwd.hpp` 第 1–45 行。

`rtti_fwd.hpp` 不含实现、不 include 任何格式适配器,可从任意翻译单元安全包含。

### 6.1 `version_tag`

```cpp
struct version_tag {                 // rtti_fwd.hpp:9–13
    int major = 0;
    int minor = 1;
    int patch = 0;
};
```

编译期库版本信息。(注意:该结构体的默认值是硬编码的,独立于生成的
`version.hpp` 宏。)测试于 `tests/reflection_smoke.cpp:7–11`。

### 6.2 前置声明(定义于 `reflection.hpp`)

```cpp
class attr_arg;                                  // :16   定义于 reflection.hpp:873
struct attr;                                     // :17   定义于 reflection.hpp:926
struct field_attr_entry;                         // :18   定义于 reflection.hpp:932
struct field_attr_list;                          // :19   定义于 reflection.hpp:938
template <typename T, typename> struct attr_table;     // :20   定义于 reflection.hpp:946
template <typename T, typename> struct field_attr_table; // :21  定义于 reflection.hpp:954
template <typename T, typename> struct template_info;   // :25   定义于 reflection.hpp:1721
```

它们属于属性系统与显式模板实参注册表(spec 20);`RTTI_TEMPLATE_INFO`
特化提供类型列表与个数。

### 6.3 门控宏 —— 在本头默认值

每个可选格式适配器由一个宏门控,未定义时**默认 1**,故直接
`#include <rtti/…>` 即全部启用:

```cpp
#if !defined(RTTI_ENABLE_JSON)  #define RTTI_ENABLE_JSON 1  #endif   // :29–31
#if !defined(RTTI_ENABLE_XML)   #define RTTI_ENABLE_XML  1  #endif   // :32–34
#if !defined(RTTI_ENABLE_INI)   #define RTTI_ENABLE_INI  1  #endif   // :35–37
#if !defined(RTTI_ENABLE_TOML)  #define RTTI_ENABLE_TOML 1  #endif   // :38–40
#if !defined(RTTI_ENABLE_PROTO) #define RTTI_ENABLE_PROTO 1 #endif   // :41–43
```

reflect 核心本身在所有 `RTTI_ENABLE_*` 宏均未定义时即可编译(`:28`)。

### 6.4 `RTTI_*` 配置宏总表

| 宏 | 默认值 | 默认定义位置 | 含义 |
| --- | --- | --- | --- |
| `RTTI_ENABLE_JSON` | 1 | `rtti_fwd.hpp:30` | JSON 格式适配器 |
| `RTTI_ENABLE_XML` | 1 | `rtti_fwd.hpp:33` | XML 格式适配器 |
| `RTTI_ENABLE_INI` | 1 | `rtti_fwd.hpp:36` | INI 格式适配器 |
| `RTTI_ENABLE_TOML` | 1 | `rtti_fwd.hpp:39` | TOML 格式适配器 |
| `RTTI_ENABLE_PROTO` | 1 | `rtti_fwd.hpp:42` | protobuf 格式适配器 |
| `RTTI_ENABLE_FLATBUFFERS` | 1 | `flat.hpp:86`、`flat_schema.hpp:73`(⚠ 不在 `rtti_fwd.hpp`) | FlatBuffers 格式适配器 |
| `RTTI_ENABLE_FLAT` | ON | CMake 选项,`CMakeLists.txt:14` | CMake 构建开关,映射到 `RTTI_ENABLE_FLATBUFFERS` 门控的源码 |

其它 CMake 构建选项(非格式门控):`RTTI_ENABLE_LTO`(默认 ON,
`CMakeLists.txt:19`);以及 `RTTI_ENABLE_JSON/XML/INI/TOML/PROTO/FLAT` 的选项
默认值(全 ON,`CMakeLists.txt:9–14`),经 `target_compile_definitions` 传给
编译器(`CMakeLists.txt:63–70`)。

**门控语义。** 格式适配器头在门控求值为 0 时 `#error`,如
`json.hpp:46–47`("RTTI: JSON format adapter not enabled (compile with
-DRTTI_ENABLE_JSON=1)")。CMake 相应地从库中条件剔除对应 `src/*.cpp`
(`CMakeLists.txt:40–54`)。`tests/reflection_smoke.cpp:19–23` 校验门控宏(若已
定义)为 0 或 1;`tests/naming_config_test.cpp:40–42` 展示了使用者常见的
`#if (!defined(GATE) || GATE)` 守卫写法。

---

## 7. 相关文档与版次

本参考只覆盖工具/支持小头簇。这些头的其他消费方:`include/rtti/convert.hpp`
(桥接)、`include/rtti/to_string.hpp`(文本格式化),以及四个格式适配器
(`json.hpp`、`xml.hpp`、`ini.hpp`、`toml.hpp`——它们经 `naming_policy` 走
命名路由)。

English edition: [../en/utils.md](../en/utils.md)。
交叉引用:[README.en.md](../../../README.en.md)、
[README.zh-CN.md](../../../README.zh-CN.md)。