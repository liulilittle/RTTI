# 非模板化迁移影响评估(Non-template migration assessment)

> **本表为迁移评估期快照;表内"候选/在迁/预计/后续批次"表述均已过时。全部批次已于 HEAD `72aaffb..0152550` 完成迁移**(base_convert / naming / date_time / scan / ini / json / toml / text_node / proto_schema / xml),现表仅作历史决策记录。当前 `src/` 静态库共 19 个编译单元:
>
> `base_convert.cpp` `common.cpp` `date_time.cpp` `error.cpp` `flat.cpp` `flat_schema.cpp` `format_reflected.cpp` `ini.cpp` `json.cpp` `memory.cpp` `naming.cpp` `number_format.cpp` `proto_schema.cpp` `reflect.cpp` `scan.cpp` `string_ops.cpp` `text_node.cpp` `toml.cpp` `xml.cpp`
>
> 迁移后全绿:`rtti_tests` 55696/0、剥离 build-off 32790/0、`rtti_proto_extension_tests` 160/0、`rtti_flat_schema_tests` 408/0(均 Debug 权威,0 error 0 warning)。

> Scope: RTTI 库(include/rtti/*,src/*.cpp,C++17,静态库 `rtti`)。目标:把"能非模板实现"的代码从 header 挪到 `src/*.cpp`(库吸收,头只留模板壳/薄转发),减少"模板满天飞"。
> 本评估只读;不审核并行在途文件。生成日期 2026-10-07。并行纪律:禁止改动并行任务在改的头与测试(列表见文末)。

## 1. 评估维度

- **符号/位置**:函数或类成员的当前落点。
- **模板依赖**:`无`(彻底非模板)、`弱`(无模板参数,仅被模板间接调用)、`强`(依赖模板参数/必须留在头)。
- **迁移成本**:低(剪贴+声明)、中(需拆接口/转发)、高(状态机/类内部逻辑缠绕)。
- **风险**:头文件消费者 ABI、inline 语义、ODR、并行冲突。
- **优先级**:高(纯函数低风险)/中(需小重构)/低(依赖模板或强耦合)。

## 2. 全量头文件评估表

读取范围:`include/rtti/*.hpp` + `include/rtti/detail/scan.hpp`。

| 头文件 | 符号/片段 | 当前位置 | 模板依赖 | 迁移成本 | 风险 | 优先级 | 备注 |
|---|---|---|---|---|---|---|---|
| base_convert.hpp | `default_digits/rfc4648_digits/digit_table_len/default_case_insensitive/base_digit/to_base/to_base_signed/from_base×4/from_base_signed` | inline 定义 | 无 | 低(整体搬移→`src/base_convert.cpp`+头留声明) | 低:纯函数、无头依赖(仅 `<rtti/error.hpp>`);被 convert.hpp/to_string.hpp 经命名空间调用,声明兼容即可 | **高** | **首批首选**。`default_digits()` 静态表只能有一份——挪 .cpp 后仍是同一 TU 内局部 static,无 ODR 问题 |
| naming.hpp | `to_snake_case/to_camel_case/apply_naming` | inline 定义 | 无(仅借用 `string_view`) | 低 | 低:纯字符串;但被 convert/ini/json/xml/toml/reflection 的 inline 路径调用(弱依赖方向),挪后 formatter 仍可链库 | **高** | 批次 2 首选。唯一注意:`apply_naming` 内 `rename_map->find(std::string(field))` 构造临时,挪后行为不变 |
| detail/scan.hpp | `char_mask/single/from_chars/set/test/find_first_of/find_first_not_of/skip_whitespace/json_structural_mask/json_string_mask/xml_special_mask/whitespace_mask/is_structural_or_string` | inline + 匿名 ns + SSE2 条件编译 | 无 | 中(SSE2 `#if` 需保留到 .cpp;`char_mask` 是 POD 也可留头) | 中:`find_first_of` 有 SIMD/标量双路径,搬移必须保留 `RTTI_DETAIL_HAS_SSE2` 位;被 json/xml 大量调用但经 `rtti::detail::` 限定,声明兼容 | 中-高 | 若做,建议只搬 `find_*` 主体或整文件;需 CMake 加 `src/scan.cpp`。**scan.hpp 有并行未提交改动**(json_string_mask),列入并行冲突区,建议推迟 |
| to_string.hpp | `to_string<T>/from_string<T>` 薄壳、`to_string_base/from_string_base×6`、`parse_integral/parse_real/parse_date_time` | inline 模板/函数 | 强(泛型分发)+弱(parse_* 无模板) | 高 | 高:`rtti::to_string<T>` 是公开 API;parse_* 是 formatter 解析底座,被 format_reflected 依赖 | 低(parse_* 中) | **并行在途**(to_string.hpp 未提交但被 format 任务不断改),本任务不加"已在迁"字样但列入后续批次 |
| convert.hpp | `scalar_*_to_* / scalar_*_from / to_value_base/from_value_base / to_value_named / 容器/元组 converter` | inline 模板为主 | 强 | 高 | 高:核心转换链,模板特化众多;非模板桥(elapsed)极少 | 低 | 并行在途;仅 to_value_base 家族(5 个纯函数,无模板)理论可搬,但消费者全在模板链内,ROI 低 |
| proto_schema.hpp | `wire_of_scalar/is_alpha/is_digit/is_name/scanner/parser::* (26 inline)` | inline(parser 状态机) | 无(parser 无模板) | 中 | 中:状态机搬到 .cpp 需把 `parser` 类私有成员+方法定义分离;`schema_desc/schema_message/schema_field` 数据类仍留头 | 中-高 | **强候选(batch 2)**:`.proto` 解析器是纯状态机。但 proto_schema.hpp 有并行未提交改动(proto 任务),本任务不触碰 |
| proto.hpp | wire 编解码、varint、extension 注册表调用侧 | inline 函数 | 弱-强(模板桥 + 非模板 wire) | 高 | 高:`schema<T>` 是模板壳,wire 部分可搬但入口全在模板特化 | 低-中 | 并行在途(proto 任务);wire 常量大,搬移收益有限 |
| json.hpp/xml.hpp/ini.hpp/toml.hpp | 各文档类/parser/serialize 内联定义 | inline(文档类、parser、serialize) | 弱(文档类非模板,仅顶层 `convert<T>` 桥模板) | 高 | 高:document/parser 是类,方法外移需头外定义(类声明留头);四个格式各一个 parser | 中 | 收益高(json/xml inline 最多),但都是**并行在途**(json/xml/ini/toml 均有未提交改动),列入后续大批次,须等相应任务合入后再做 |
| date_time.hpp | `date_time` 类成员、parse/format、epoch 换算 | inline | 弱 | 中 | 中:date_time 已提供 `format(pattern)` 经 src/format_reflected 调用;类方法外移需声明/定义分离 | 中 | 并行在途(format 任务在用 date_time::format);后续批次 |
| reflection.hpp | `RTTI_DEFINE_*` 宏、模板注册、attr 系统 | 模板/宏 | 强 | 高 | 高:反射注册必须模板+宏;attr 表有模板参数 `attr_table<T,Traits>` | 低 | 基本不适合手动非模板化;枚举/attr 辅助可留待专项 |
| reflect.hpp | `reflect_*` 查询、type_judge、迭代器 | 模板 | 强 | 高 | 高 | 低 | 模板核心,不迁移 |
| node_io.hpp | `locate/node_as/to_node` 壳、`value_io` 原生方法 | 模板壳 + inline | 弱(locate 非模板) | 低 | 低:locate 核心由 Task7 放 src/reflect.cpp 了 | 中(收尾核验) | 见下文"已入 src"清单;本任务仅评估 |
| error.hpp | `to_string(error_code)`、`error_info::line/column/message` | 已外移 src/error.cpp | — | — | — | — | **已迁移** |
| memory.hpp | `memory_arena` 成员(alloc/dup/reset/block) | 已外移 src/memory.cpp | — | — | — | — | **已迁移**(`alloc_obj` 模板留头) |
| number_format.hpp | `format_number_unsigned/signed/real/parse_number_spec/reflected_fields_to_string` | 声明在头,定义在 src/number_format.cpp + src/format_reflected.cpp | — | — | — | — | **已迁移**(Task8 完成,含模板壳与 src 定义分离) |
| string_ops.hpp / src/string_ops.cpp | `str::to_base/from_base/…split/join/trim/…` | 声明 + `?? src/string_ops.cpp`(新建未提交) | 弱 | — | — | — | **在迁**(并行任务 8a,工作树已出现 src/string_ops.cpp) |
| flat.hpp/ flat_schema.hpp | flat wire 编解码 / .fbs 文本解析 | 壳在头,定义在 src/flat.cpp + src/flat_schema.cpp | — | — | — | — | **已迁移**(Task5/6 完成并提交 6612f5c/e23d7b7) |
| ioc.hpp | ioc 容器、singleton | inline 模板 | 强 | 高 | 高 | 低 | 模板容器,不迁移 |

## 3. 首批建议(Batch #1,文档期不落地)

**Band A:高优先级纯函数(无模板依赖、无并行文件冲突)** — 推荐实际迁移顺序:

1. **base_convert.hpp → `src/base_convert.cpp`**
   - 符号:本文件全部 12 个函数(数字表访问器 + to_base/from_base 族)。
   - 理由:零模板、零 include 依赖(仅 error.hpp)、纯数字算法、无边角 ABI(无类成员)。
   - 做法:`.cpp` 内定义,头留原型 + 默认实参;`default_digits()` 的局部 static 表随定义入 .cpp(单一 static,无 ODR)。
   - 回归:base_convert_test.cpp(27 用例)+ string_convert_test + convert_test 全绿即行为零变化;共有测试已包含 base_convert。
2. **naming.hpp → `src/naming.cpp`**(batch 2 候选)
   - to_snake_case / to_camel_case / apply_naming。注意三头 convert/ini/json/xml/toml 都在 include `rtti/naming.hpp`,搬移必须保证声明仍在头,否则断链。
   - 此项与 convert.hpp 并行在途有弱关联(只经命名空间调用),建议在 batch1 之后、或复检 convert 已合入时再做。

**Band B:强但需等并行窗口:**

3. **detail/scan.hpp** — scan.hpp 有并行未提交改动(`json_string_mask`),先列后等;且 SSE2 双路径搬移成本中。
4. **proto_schema.hpp parser** — 纯状态机,收益大;等 proto 任务(Task2/3/4)合入后再动。

## 4. 后续批次(按优先级)

| 批次 | 目标 | 前置依赖 | 预计收益 |
|---|---|---|---|
| 2 | naming.hpp→src/naming.cpp | convert.hpp 稳定 | 中(纯函数去 inline) |
| 2 | proto_schema parser 状态机→src | proto 任务合入 | 高(最大 inline 块之一) |
| 3 | json/xml/ini/toml document+parser 外移 | 各格式任务合入 | 高(inline 数量最多) |
| 3 | date_time 类方法外移 | format 任务合入 | 中 |
| 4 | to_string.hpp parse_*、convert.hpp 非模板桥 | 全链稳定 | 中 |
| 低优先 | reflection/reflect/ioc 模板核心 | — | 低(本就不该去模板化) |

## 5. 并行冲突区(本任务不触碰)

- `include/rtti/{proto,proto_schema,flat,json,xml,ini,toml,reflect,reflection,to_string,date_time,node_io}.hpp`
- `src/{reflect,error,common,memory}.cpp`
- `tests/fixtures/*`
- `include/rtti/detail/scan.hpp`(存在并行未提交改动 `json_string_mask`)
- `string_ops.hpp` / `src/string_ops.cpp`(并行任务在迁)

## 6. 结论

- **已达成非模板落库**:error/memory/number_format/format_reflected/flat/flat_schema 均已在 `src/*.cpp` 或声明+src 分离。架构方向被验证可行。
- **首批可安全迁移(零行为变化、低风险)**:`base_convert.hpp`(首选)、`naming.hpp`(次选)。
- **本任务(评估文档)不落地代码**;实际迁移建议由后续修复/执行任务按 Band A 顺序执行,每个迁移后跑全量回归 0 failure。