# 迁移后深度研究优化实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 对迁移后代码布局(头声明化+src/*.cpp)执行二次深度审计、注释专业补强、文档体系同步,并修复全部六项协议缺口,确保"绝对无问题、绝对好、注释文档专业详细"。

**Architecture:** 三只读审计团(核心链/格式/proto-flat)复核迁移后正确性 + 采集注释/文档差异清单 → 双审交叉复核 → 并行 TDD 修复组(注释补强/文档同步/六项缺口) → 审计团 scoped re-review → 主代理统一终验 + 一次性报告。与项目既有"审计→修复→双审"工作流一致。

**Tech Stack:** C++17, MSVC2022, CMake("Visual Studio 17 2022" -A x64), 自研 harness(RTTI_TEST_CASE/RTTI_CHECK), /W4 /permissive- /Zc:preprocessor 零警告硬门。

**Spec:** `docs/superpowers/specs/2026-10-08-migration-verify-comments-docs-gaps-design.md`(commit 0152550)。本计划自此 spec 推导。

## Global Constraints

- 基线(Debug 权威):主 rtti_tests **55696/0**、剥离 build-off **32790/0**、rtti_proto_extension_tests **160/0**、rtti_flat_schema_tests **408/0**,双构建 0 error 0 warning。/W4 /permissive- /Zc:preprocessor。
- 头文件(includes/rtti/)仅保留公开声明+模板壳+内联模板必需物;所有新增非模板实现必须进 src/*.cpp(用户硬性要求,禁止写回头)。
- 默认实参只留头声明,定义去重;ODR 全 include 树唯一;vtable 在关键 TU(dtor)发射。
- 注释全英文、引用 spec 章节(如 §x.y)、与签名/实现一致。
- 测试只测改动相关项(C10);全量门仅阶段 5 统一跑一次。
- 所有功能修复先红后绿(TDD);失败记入 discover.md。
- src 文件列表(19):base_convert/common/date_time/error/flat/flat_schema/format_reflected/ini/json/memory/naming/number_format/proto_schema/reflect/scan/string_ops/text_node/toml/xml。
- include 文件列表(22):base_convert/convert/date_time/error/flat/flat_schema/ini/ioc/json/memory/naming/node_io/number_format/proto/proto_schema/reflect/reflection/rtti_fwd/string_ops/toml/to_string/xml(.hpp)。

## Review Focus

- 头文件在新代码布局下 ODR 违反 / 声明与 src 定义签名不一致 / 默认实参重复(二次审计重点;每模块核对项)。
- vtable 发射 TU 缺失导致链接期 missing vtable(迁移后新风险;审计核对析构定义位置)。
- 注释引用已删 API/旧行号/过时语义(迁移把函数移 cpp,头注释可能仍引用老位置)。
- G-2 enum 值超 int32 静默截断(现 1627 行双截断);G-1 extension 解析全局无作用域(register_schema_extensions 2600 区无 scope)。
- G-6 toml.hpp:26-29 "never throws" 与实际 pool_alloc(new→throw)冲突。

---

### Task 1: 三审计团迁移后二次审计 + 注释/文档差异清单(只读)

**Files:**
- Read-only: `include/rtti/*`(22 头)、`src/*`(19 cpp)、`README.md`、`README.en.md`、`README.zh-CN.md`、`docs/superpowers/notes/nontemplate-migration.md`

**Interfaces:**
- Consumes: HEAD=72aaffb 基线源码布局。
- Produces: 三份审计报告(每份含 [Correctness/ODR/vtable/签名/行为抽查] + [CommentDefs: 缺注释/过期注释/不一致/风格] + [DocSync: README 差异]+ 六项缺口现状核实)。

- [ ] **Step 1: 三审计团派发(只读,不改文件)**
  复用既有审计会话(task 新开三个 read-only 子代理):
  - 审计A-核心链:reflection/reflect/convert/to_string/node_io/ioc/base_convert/naming/string_ops/number_format/memory/error/date_time + 对应 src/*.cpp。
  - 审计B-格式:json/xml/ini/toml + detail/scan + 对应 src/*.cpp。
  - 审计C-proto/flat:proto/proto_schema/flat/flat_schema/rtti_fwd + 对应 src/*.cpp。
  每团按要求:①核对头声明与 src 定义签名逐项一致(默认实参只留头)、ODR 唯一、vtable 在 dtor TU;②注释核对(缺块/过期/不一致/风格);③README 双语+迁移评估与实现同步;④六项缺口对号核实现状(尤其 G-4 \u 代理对:确认真实缺口在 json/text_node/toml 已修?XML 数字实体经 valid_xml_char 已拒代理区?)。每团 relay 报告(中文,含 file:line 证据)。

- [ ] **Step 2: 主代理聚合三报告入 .opencode/process.md**,分类为 [Correct/Imp/Minor] 与 [Comment]/[Doc]/[G-1..G-6]。

- [ ] **Step 3: 双审复核**
  审计A/B 交叉复核对方 Correct 结论(合理性/权威一致);审计C 复核 B、A 复核 C 的六项缺口核实。盖章后生成最终审计裁决(只读)。

### Task 2: 注释专业补强(并行修复组 F-X1)

**Files:**
- Modify: 审计报告列出的注释问题文件(头+src)
- Test: 相关套件补注释性断言(如文档化 error_code 契约测试)——纯注释不可测试,以审读+构建验证

**Interfaces:**
- Consumes: Task 1 注释差异清单。
- Produces: 注释与实现/签名一致、无过期引用、全英文、spec 引用、风格统一。

- [ ] **Step 1: 按审计注释清单逐文件修复**(缺注释补、过期改、不一致修、风格统一)。
- [ ] **Step 2: 构建验证**:`cmake --build build --target rtti --config Debug`,0 error 0 warning;受影响测试套件绿。
- [ ] **Step 3: Commit** `docs: professional comment polish across headers/src [X-list]`。

### Task 3: 文档体系完善(修复组 F-X2)

**Files:**
- Modify: `README.md`、`README.en.md`、`README.zh-CN.md`、`docs/superpowers/notes/nontemplate-migration.md`

**Interfaces:**
- Consumes: Task 1 文档差异清单 + 最新实现事实(src 21 cpp、头 22、门控 6)。
- Produces: README 双向镜像与实现同步(门控数、API 清单、迁移状态表、基线断言数)。

- [ ] **Step 1: 按差异清单核对 README 双语+migration 笔记**(API 清单/门控/剥离矩阵/迁移状态/基线数)。
- [ ] **Step 2: 修正不同步点**,保持中英镜像(README.md 导航、en 与 zh-CN 逐节对称)。
- [ ] **Step 3: Commit** `docs: sync README bilingual + migration notes [G]`。

### Task 4: G-1 extension/extendee 作用域解析(修复组 F-G1)

**Files:**
- Modify: `src/proto_schema.cpp`(resolve_types 后 extension 解析段 ~2600 区、register_schema_extensions:2692-2709)
- Test: `tests/proto_schema_test.cpp`

**Interfaces:**
- Consumes: src/proto_schema.cpp 既有 resolve_message_ref(d,full,simple,scope,norm,out) 签名。
- Produces: register_schema_extensions 对 extension 的 extendee/字段类型按所属 extend 块消息作用域解析(与 resolve_types 的 scope 链一致;包限定名保留绝对行为)。

- [ ] **Step 1: 写失败测试** 遮蔽场景:两消息各有同名 Inner,extension 字段类型指向本消息 Inner。红。
- [ ] **Step 2: 实现** 传入 extend 块作用域,scope-aware 解析;绝对引用(前导点)不受影响;空 scope 保留兼容。
- [ ] **Step 3: 绿** 新断言绿 + proto_ext 160/0 官方对拍保持。
- [ ] **Step 4: Commit** `fix(proto): scope-aware extension field resolution [G-1]`。

### Task 5: G-2 enum 值超 int32 校验(修复组 F-G2)

**Files:**
- Modify: `src/proto_schema.cpp`(parse_enum :1627 段)
- Test: `tests/proto_schema_test.cpp`

**Interfaces:**
- Consumes: parse_enum 内 cur token values、error_code 集。
- Produces: enum 值不在 int32 范围 → `error_code::proto_bad_field`(protoc 语义,拒绝而非静默截断)。

- [ ] **Step 1: 写失败测试** `enum E { A = 2147483648; }` 应 proto_bad_field;`A = -2147483649` 同;边界 2147483647/-2147483648 合法。红(现静默截断通过)。
- [ ] **Step 2: 实现** 解析后加 fits_int32 校验(uv 超 0x7FFFFFFF 或负向超 0x80000000 → set_error(proto_bad_field))。
- [ ] **Step 3: 绿** 新断言绿 + proto 相关套件绿。
- [ ] **Step 4: Commit** `fix(proto): reject enum values outside int32 [G-2]`。

### Task 6: G-3 flat u64 typed 编解码保真决策+落地(修复组 F-G3)

**Files:**
- Modify: `include/rtti/flat.hpp`(u64 boundary 文档)、`src/flat.cpp`(:553-561 decode 钳制、encode 路径)
- Test: `tests/flat_test.cpp`

**Interfaces:**
- Consumes: flat.hpp/ful decode clamp 现状、convert overflow 桥。
- Produces: 决策落地:typed serialize<u64>(≥2^63)返回明确 overflow(已有)并补 README/头注释强制文档;decode 保留钳制但保证 tree 值无损(2^63-1 以内);交叉测试锁死。

- [ ] **Step 1: 审计裁决** 修复组按 §0 决策 G-3 取向(文档化强制 vs 保真重构——取文档化强制,因 wire 层 u64 表示变更会破坏官方对拍,风险>收益)。将决策 relay 审计团确认。
- [ ] **Step 2: 补测试** FlatU64TypedOverflowExplicit:typed serialize 2^63 返回 overflow 且错误码明确;2^63-1 无损往返;++add 断言。
- [ ] **Step 3: 文档** flat.hpp u64 boundary 段落补 typed 路径明确报酬;README caveat 同步。
- [ ] **Step 4: 绿+Commit** `fix(flat): document typed u64 overflow contract + test [G-3]`。

### Task 7: G-4 \u 代理对校验缺口修复(修复组 F-G4)

**Files:**
- Modify: 审计确认的缺口文件(预期 xml.cpp 引用实体或其它;json/text_node/toml 已有校验,仅核验)
- Test: 对应测试(xml_test 或其它)

**Interfaces:**
- Consumes: Task 1 审计C 对 G-4 现状核实结论。
- Produces: 缺失路径补 `\u` 代理对(高代理必须跟低代理、孤立高低代理拒绝)错误→ error_code::invalid_utf8;已实现路径补测试锁定。

- [ ] **Step 1: 按审计缺口定位写失败测试**(若全部已实现,则改为补测试锁定+记录 G-4 已在 json/text_node/toml 覆盖、XML 数字实体经 valid_xml_char 拒代理区→仅补测试)。
- [ ] **Step 2: 实现/验证** 缺失路径补校验;已实现路径仅验证。
- [ ] **Step 3: 绿+Commit** `fix(xml): surrogate-pair validation gap [G-4]`(或 `test: pin surrogate validation coverage`)。
- [ ] **Step 4: 若 G-4 全部已覆盖**,本任务降级为"补测试 + 文档断言",在报告中说明(不阻塞)。

### Task 8: G-5 包限定 extendee 名支持(修复组 F-G5)

**Files:**
- Modify: `src/proto_schema.cpp`(register_schema_extensions extendee 解析)
- Test: `tests/proto_schema_test.cpp`

**Interfaces:**
- Consumes: register_schema_extensions 现有 extendee 匹配逻辑。
- Produces: 包限定 extendee 名(`package.Foo`)正确解析;不带包名保留相对解析。官方 protoc 对拍 160/0 保持。

- [ ] **Step 1: 写失败测试** 包 `A` 中 extend `.B.Message`(绝对)与包内相对——当前不支持,红。
- [ ] **Step 2: 实现** extendee 名按"带点=逐级剪前缀、前导点=绝对"解析(复用 resolve 语义)。2630 区逻辑扩展。
- [ ] **Step 3: 绿+官方对拍** proto_ext 160/0;新断言绿。
- [ ] **Step 4: Commit** `fix(proto): support package-qualified extendee names [G-5]`。

### Task 9: G-6 ini/toml 分配策略统一/文档化(修复组 F-G6)

**Files:**
- Modify: `include/rtti/toml.hpp`(:26-29 注释)、`include/rtti/ini.hpp`(pool_alloc 若需)、`src/toml.cpp`(pool_alloc 实现)
- Test: `tests/toml_test.cpp`、`tests/ini_test.cpp`

**Interfaces:**
- Consumes: toml.hpp pool_alloc(new → throw bad_alloc)、json/xml arena(null) 现状。
- Produces: 决策:pool_alloc 捕获 new 失败返 null(error_code 面)或文档化差异(改 toml.hpp:26 "never throws" 措辞为如实)。修复组按 §0 裁决并 relay 审计团。

- [ ] **Step 1: 审计裁决** 决策:统一为 null 语义(改 pool_alloc)vs 文档化(改注释措辞)。倾向:文档化(行为变更风险大,与 json './xml 分离路径不同元)除非审计要求统一。
- [ ] **Step 2: 落地** 若文档化:toml.hpp:26 "never throws" 改"minmax throws only on allocation failure(经 pool_alloc new);normal parse_path returns error_code"式如实表述;若统一:pool_alloc 返回改为捕获 bad_alloc→nullptr 且全部调用点判空。
- [ ] **Step 3: 绿+Commit** `fix(toml/ini): honest allocation-failure contract [G-6]`。

### Task 10: 统一终验 + 一次性报告 + update_goal complete 评估(主代理)

**Files:**
- Verify: build 与 build-off 双构建 + 四套件

**Interfaces:**
- Consumes: Task 2-9 全部 commit。
- Produces: 终验证据与一次性报告。

- [ ] **Step 1: 主代理独立终验** `cmake --build build --config Debug` 0e0w;rtti_tests/proto_ext/flat_schema 断言数(≥基线);build-off 32790/0。
- [ ] **Step 2: W6 逐条验收**(四项范围+六项缺口修复现状+注释/文档状态)写一次性报告。
- [ ] **Step 3: 记入四文件**(process.md 归档 + memory.md 裁剪纪律 + plan.md 完成)。
- [ ] **Step 4: update_goal complete 评估**(存在延续方向则禁止 complete;否则带证据 complete)。