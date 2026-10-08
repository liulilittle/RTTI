# 迁移后深度研究优化与注释文档增强设计

- 日期:2026-10-08
- 状态:定稿(用户批准方案 A:分层团队/双审闸门,全部六项缺口修复)
- 基线:HEAD=72aaffb;主 rtti_tests 55696/0、剥离 build-off 32790/0、proto_ext 160/0、flat_schema 408/0,双构建 0 error 0 warning(Debug 权威)。

## 背景与目标

已完成:三团并行审计(核心链/格式/proto-flat)→ 真问题修复(2 Critical + 8 Important + 多 Minor,双审 Accepted)→ 8 批非模板迁移(头声明化+src/*.cpp,行为零变化)。当前代码布局发生变化:22 个公共头文件(部分仅余声明+模板壳)、19 个 src 实现文件。

用户要求"继续深度研究优化,确保绝对无问题、绝对好,注释、文档写得非常专业、详细"。范围(用户确认四项全选 + 全部六项缺口修复):

1. **迁移后二次深度审计**:对头声明化+src/*.cpp 新布局重做完整正确性审计,不只复用迁移前结论。
2. **注释专业检查与补强**:逐公共头文件+src 检查注释——缺注释补、注释与实现/签名一致、风格统一。
3. **文档体系完善**:README 中英双语与最新实现同步核验修正。
4. **新增功能/修复协议缺口**:全部六项。

## 六项协议缺口(全部修复,各自 TDD)

| # | 缺口 | 位置 | 出处 |
|---|------|------|------|
| G-1 | extension/extendee 类型解析仍全局无作用域(未走 scope 链) | proto_schema.hpp/cpp resolve 路径 | 审计C 复核遗留建议① |
| G-2 | enum 值超 int32 静默截断(应 fits_int32 校验) | proto_schema.hpp parse_enum 1775-1776 | 审计C 原报告 PS2 Minor / 复核遗留② |
| G-3 | flat u64 typed 编码依赖 convert overflow 拒绝(路径可借) | flat.cpp:553-561+convert 桥 | 审计C D-4 遗留观③ |
| G-4 | \uD800-\uDFFF 代理对未校验 | json.hpp/xml.hpp string 解码 | 深度验证期 M-3 剩余④ |
| G-5 | 包限定 extendee 名不支持 | proto.hpp register_schema_extensions / proto_schema 解析 | 遗留⑤ |
| G-6 | ini/toml pool_alloc(new→bad_alloc) vs json/xml arena(null)分配策略差异未统一(toml.hpp:26 "never throws" 措辞违规) | ini.hpp/toml.hpp 分配面 | 审计B 复核遗留⑥ |

- G-1/G-2/G-4/G-5 属正确性/一致性修复,必须红→绿测试驱动。
- G-3 需决策:文档化强制(typed serialize 返回 overflow 保持)vs 保真修复(改变 u64 表示)——由修复组按 §0 前置检查裁决并报将。
- G-6 需决策:统一为 arena(null) 语义(改 pool_alloc 失败路径)vs 文档化差异(toml.hpp:26 措辞修正)——由修复组裁决并报将。

## 工作分解与合规性

### 阶段 1:三只读审计团(迁移后二次审计 + 注释/文档差异清单)
只读,不改文件。每团产出:
- 正确性审计:ODR 全树唯一性、vtable 在关键 TU(析构)发射、头声明与 src 定义签名逐一一致(默认实参只留头)、命名空间一致、行为等价抽查(独立编译 src + 独立测试链接新库=基线)。
- 注释差异清单:缺注释块、过期注释(引用已删老 API 行号/旧语义)、注释与签名不一致、Doxygen 可文档化但缺失、风格不一致(本项目规范:全英文、块注释引用 spec 章节如 §x.y、C++17 无第三方)。
- 文档差异清单:README.en/zh-CN/md 与实现同步性(门控数、API 清单、剥离矩阵、示例)。

分工:
- 审计A-核心链:reflection/reflect/convert/to_string/node_io/ioc/base_convert/naming/string_ops/number_format/memory/error/date_time + 对应 src。
- 审计B-格式:json/xml/ini/toml + detail/scan + 对应 src。
- 审计C-proto/flat:proto/proto_schema/flat/flat_schema/rtti_fwd + 对应 src。

### 阶段 2:双审复核
审计 A/B/C 交叉复核对方审计发现的合理性(角色隔离,禁止单一代理代全流程)。

### 阶段 3:TDD 修复组(并行)
- F-X1 注释补强:按审计清单逐文件补/改注释(全英文、spec 引用、与签名一致)。
- F-X2 文档同步:README 双向 + 设计/迁移评估文档与实现同步。
- F-G1 extension 作用域解析。
- F-G2 enum fits_int32 校验。
- F-G3 flat u64 决策+落地。
- F-G4 \u 代理对校验(json+xml)。
- F-G5 包限定 extendee 名。
- F-G6 分配策略统一或文档化。
每组独立 commit,红→绿证据。

### 阶段 4:双审复核修复
审计团 scoped re-review 各修复组 commit,[Accepted/Rejected]+evidence。

### 阶段 5:统一终验 + 一次性报告
主代理独立复验:主 55696+增量/0、剥离 32790/0、proto 160/0、flat 408/0、双构建 0e0w。一次性报告(审计发现、注释/文档现状、六项缺口修复决策+结果、终验基线)。

## 硬约束(G1-G5、W1-W11、C0-C10 全程适用)
- 非模板实现必须留在 src(用户硬性要求,防止编译体积回退);修复期如有新增非模板代码必须进 src 对应文件,禁止写回头。
- 测试只测改动相关项(C10);全量门仅阶段 5 统一跑一次。
- Debug=权威;Release exe 陈旧产物。
- ODR/vtable/默认实参约束在二次审计中强制执行。
- 零警告硬门(/W4 /permissive- /Zc:preprocessor)。
- 全部完成后一次性报告,禁止过程性打扰。