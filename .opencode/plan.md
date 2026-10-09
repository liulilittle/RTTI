# 方案

## 2026-10-09 文档体系补齐闭环(当前目标,已完成)
- [x] reflection EN+ZH ch18-27 宏族追加(commit c1753eb):§18-27 全章节;布局表行号与 reflection.hpp grep 实证一致。
- [x] reflect-tools EN+ZH 新建(commit 8756fa2):reflect/ioc/to_string/string_ops 全覆盖;docs/README.md 与三 README 索引拆条接线。
- [x] performance-report EN+ZH 扩展(commit 6ddc999):§3.3/§4.1 LTCG 表 + §5 断言基线对照表;LTCG 原始输出落盘 docs/benchmarks/2026-10-09-LTCG-*.txt;节号顺延。
- [x] 事实性核查全 pass(commit b744b4b):29 md 链接扫描零断链;API 名/宏名存在性比对零缺失;bench 数字与 txt 逐字一致;修正 zh/reflection.md:616 编造 API(`attr_arg::make_int`→真实工厂)。
- [x] 工作树清理 + .gitignore(commit 8268403):build-opt/、.opencode/backups/ 忽略;docs/superpowers 历史 spec/plan 追记入库。
- [x] W6 验收:docs/interface 六对文档(en+zh:reflection/convert/formats/proto-flat/utils/reflect-tools)覆盖全部 23 个公开头;docs/design en+zh 已存在(b5cc3d0);docs/benchmarks en+zh Debug/Release/LTCG 数字表+基线对照+复现步骤;三 README 索引连通;全部事实可核。作者验讫:主 296,?|子 1,298,093,307(费用 0)。

## 2026-10-08 迁移后深度优化阶段(新目标:继续深度研究优化,注释/文档专业详细+缺口全修)
- [x] spec 0152550 + plan 6f06af1(post-migration deep-verify,10 任务)用户批准 + Subagent-driven 执行。
- [x] Task1 三审计团二次审计(核心链/格式/proto-flat)全 ACCEPT:正确性零缺陷;注释/文档过期清单 + 缺口实证(G-1/G-2/G-5 缺失,G-3/G-4 已实现,G-6 措辞冲突)。
- [x] Task2 注释补强 7a3ddc5(10 项,task review 10/10 + 审计盖章)。
- [x] Task3 文档同步 a63ab1b(README en/zh 镜像+断言刷新+migration 快照;审计盖章)。
- [x] 缺口组 G-2 ce6312c / G-1 dd5e623 / G-5 f23da97(TDD 红绿;审计C 复核 ACCEPT;官方对拍 160/0 保持)。
- [x] Task6/7 降级验证 4b0f533(G-3 u64 + G-4 代理对补测试 47 断言;审查+复核 ACCEPT)。
- [x] C9 同型:审计B 六处 never-throws 扫描,裁定 4 非同类,number_format.hpp:141/150 同型修复 92291da。
- [x] scoped 三审计复核(A/B/C)全 ACCEPT;统一终验主 55774/0 + 剥离 32800/0 + proto 160/0 + flat 408/0,0e0w;子代理全部解散。

## 阶段:反射核心层实现(Project = RTTI)

- [x] brainstorming + spec 定稿(反射核心 + JSON/XML/INI 三格式可剥离适配器)
- [x] writing-plans 计划文档: docs/superpowers/plans/2026-10-05-rtti-reflect-core.md (12 任务)
- [x] 执行完成: Task 1-12 全部完成,双构建测试 166/0

## 关键决策
- 命名空间 = `rtti`,项目名 RTTI
- C++17 硬约束(全平台),成员指针访问器(非 offsetof)
- 编译开销可控:反射非病态用例,禁止武断字段数上限,但避免递归宏自展开/病态模板递归
- 代码+注释全英语;文档中英双语互索引
- 测试框架:自研极简 harness(RTTI_TEST_CASE/RTTI_CHECK),test_main.cpp 单点 main

## 扩展阶段(用户需求 2:容器操作 + 适配器 + 类型描述)
- [x] 适配器家族:queue/stack/priority_queue 转换(顺序语义重建)
- [x] 运行时容器操作:add/erase_at/erase_key/clear/size/for_each(field_access_base 虚方法 + reflect.hpp API)
- [x] 类型描述系统:type_desc + type_of<T>(惰性 static + resolver 延迟解析)
- 验收:双构建(主 build + build-off)测试 329/0,零警告

## 工程实现阶段(用户需求 3:JSON/XML/INI 格式适配器)
- [x] JSON 适配器:DOM + parse/serialize + parse_as<T>/serialize<T> 反射桥接;200/0 独立核验
- [x] XML 适配器:元素/属性/文本/CDATA/注释/PI/实体/命名空间/DTD 内部子集;value_io 映射(属性/子元素/文本/重复元素 array);188/0 独立核验
- [x] INI 适配器:节/键值/注释/引号转义/重复键/重复节 array;修复 SSO 悬垂后 160/0 独立核验
- 双构建全绿:主构建(三门全开)877/0、build-off(三门全关)329/0,零警告
- 集成修复:枚举转换统一移入 convert.hpp 核心(is_registered_enum 守卫);测试枚举跨 TU 唯一命名;tests/CMakeLists 按开关条件追加格式测试
- 验收:三格式序列化/反序列化完整交付,无遗留功能缺口

## 收尾阶段(补齐 spec 缺口,用户要求"无任何可优化/可实现方向遗留")
- [x] 流式 SAX API(§7.2):JSON/XML/INI 三格式统一事件回调(object_start/array_start/key/value/element_start/attribute/section_start 等),DOM 解析内部构建于流式之上,流式独立暴露供零树路径
- [x] serialize(root, buffer) 预分配入口(§7.1):每个格式除返回 std::string 外补 std::string& 目标缓冲重载
- [x] 剥离矩阵八组合(§8):仅核心/核心+JSON/核心+XML/核心+INI/核心+JSON+XML/核心+JSON+INI/核心+XML+INI/全开 逐一编译跑测试(全部零失败)

## 性能优化阶段(用户选定:SIMD 结构字符扫描 + MemoryArena 节点池复用)
- [x] P1 共享 SIMD 结构扫描工具 include/rtti/detail/scan.hpp(SSE2 条件编译 _M_X64/_M_IX86 + __SSE2__,标量回退;find 结构字符/跳过字符串/空白;纯函数零格式头依赖;128 位 char_mask 因 JSON 结构字符 ASCII 91-125 超 64 位)
- [x] P2 JSON 优化:node 分配改 memory_arena 节点池(owned_ 裸指针 + alloc_obj + 显式 ~node 级联;保留 move 语义与测试全绿)
- [x] P3 XML 优化:<>& 结构扫描换入 + node 分配改 arena 池(同 P2 约束)
- [x] P4 基准对照(bench/bench_parse.cpp:warm-up+checksum+30 迭代。节点池 JSON +46%/XML +24% 吞吐;SIMD 换入在紧凑短令牌基准持平(门控≥16B 防回退),checksum 341910/314550 前后一致语义零变化)
- [x] P5 回归:主构建 1245/0(选装 SIMD 抑制 scan 亦 1245/0)、build-off 607/0(329 核心+278 scan)、剥离矩阵八组合 607/839/832/788/1064/1020/1013/1245 全绿、零警告
- 验收:scan_test.cpp 无条件入 RTTI_TEST_SOURCES(核心测试);测试枚举类 UniqueKeeper 保 scan 无 ODR;harness 静态自注册

## 执行阶段(2026-10-06,SDD 多波次,主构建 32266/0 全绿)
- [x] 全部波次产物已核验(主构建 32266 assertions 0 failures 零警告):B2 新类型、C2 浮点统一、C3+C4 键哈希+SIMD、D1+D2 naming、E 迭代器、F TOML、G 数组、H protobuf、I date_time、J base_convert、K to_string、P 转换宽容、L Attribute、M 实例化、IOC 容器、A/B 自定义 allocator/less。
- [x] examples/ 7 个 demo(reflection/core_convert/json/xml/ini/toml/proto)独立 cl 编译零警告运行通过。
- [x] type_judge_test C4002 修复(N 子代理):RTTI_CHECK 宏参数内模板逗号未圆括号包裹 → 修复后 32084/0。
- [x] §20 抽象/虚函数判断+模板类型反射(子代理 ses_ef04a5c4dffefyw30XFvx4tcP3):is_abstract/is_polymorphic/has_virtual_destructor + template_info/template_args/template_arg_count/arg_at/args_of + RTTI_TEMPLATE_INFO;32163/0 主代理探针验证遗留点非缺陷。
- [x] §22 方法反射(子代理 ses_ef027c200ffeQjOHQ1LwXCcpJ3):RTTI_DEFINE_METHOD/RTTI_DEFINE_STATIC_METHOD + reflect_method 静态/T&/object_ref/instance 重载 + methods<T>()/method_count<T>() + error_code::no_such_method;32266/0。
- [x] README 双语更新(子代理 ses_ef056ac25ffeS4fz9ir0ycf11X):README.md 导航 + en/zh-CN 镜像篇章,剥离矩阵表述如实。
- [x] 剥离矩阵 32 组合全绿(主代理):五门 2^5=32 组合逐一独立配置构建测试,零失败零警告,断言 30274→32266 随门开单调递增。前置门控债修复(5 个无条件测试文件格式头门控化,子代理 ses_eefd0e28effe9sgAivD060Sq5h)。
- [x] build-off 重建全关终验:30274/0 零警告,双构建全绿(主 32266/0 + 全关 30274/0)。
- [x] 根目录探针产物清理(96 个 git 忽略 .exe/.obj/.log + sct_obj + macro_ep.txt)。
- [x] strip_matrix_test 补 TOML/PROTO 门:五门守卫与文件头注释同步(已核验全关 30274/0+全开 32266/0)。
- [x] R1 收尾:W6 全需求验收通过(目标交付七项全覆盖)、README 剥离矩阵表述更新(32 组合已全量验证)、build-off 全关终验 30274/0、strip_matrix 五门守卫补齐。
- [x] **目标完成**(update_goal complete,2026-10-06):全部需求[完成]+W5 证据(双构建 32266/0+30274/0、剥离矩阵 32 组合、examples 7 demo、fuzz 22 处)+无延续方向(事件/委托反射为 spec §21.2 明示排除)。

## 深度验证阶段(新目标,2026-10-07)
- [x] 六维审计全部收齐(核心安全/测试有效/spec 符合/遗留核查/API 文档/可移植),详见 process.md(3 Critical 栈溢出 C1-C3、I1-I8、LP64/宏空参、ODR、§13.2 解析器缺口等)。
- [x] §13.2 用户裁决:"两个都需要"——schema<T> 注册文档化 + 补 .proto 文本解析器。
- [x] 5 修复组并行派发与收口:R1 序列化转换安全 / R2 生命周期安全 / R3 宏守卫+测试 / R4 文档 / R5 .proto 解析器(全部核验,主 54738/0、剥离 32462/0)。
- [x] R5 空输出重核:proto_schema.hpp 1800 行产物确认落盘,compile_schema 桥接 wire 链路,自检 180/0。
- [x] 修复复审计(双审 A1 核心侧 / A2 测试文档侧):R1/R5 合理通过、R2 基本通过、R3 主体通过;3 项残留(R5 sint zigzag/boolean 树表示、R2-2 多句柄文档注记、R3-1 括号实参)并入新功能计划处理。

## 新功能阶段(2026-10-07 用户指令:proto V2-V3 全版本 + FlatBuffers,spec=2026-10-07-proto-v2v3-flatbuffers-design.md)
- [x] Task1 fixtures 骨架 8893bab(官方 11 文件)。
- [x] Task2 zigzag 4 应用点 b4e3ea1+47e441d 双审盖章(官方字节对拍)。
- [x] Task5 FlatBuffers wire 6612f5c+ecd0209 双审盖章(flatc 对拍;遗留 Minor D/E)。
- [x] Task6 .fbs 解析 e23d7b7+3a77063 双审盖章。
- [x] Task7 节点级转换 1352074+2c30d79 双审盖章。
- [x] Task8 字符串格式化 645e657+22ef9c4 双审盖章。
- [x] Task8a rtti::str 5851ad8 + T-A 接线 2af40ac + 复评盖章(C-1 闭合 Accepted)。
- [x] Task9 非模板迁移评估(nontemplate-migration.md)。
- [x] T-A 接线 2af40ac:9 src + FLAT gate + strip 脚本 64 组合。
- [x] FIX LNK2019 05ddb1c(reflection.hpp 尾部 include convert)。
- [x] P0 分层提交 4 commits(ca25d99/62cd1a7/81c6f6e/cd4d5a3),HEAD=cd4d5a3。
- [x] Task3 修复 I-1/I-2+7 Minor 完成(commit ddf1464d,主代理独立核验 55642/0)。
- [x] Task4 extensions registry 完成(commit 5588ba9,5 files +675:ext proproto/ext.Base.textbin/wire+proto_extension_test 注册;extensions 主体在 ddf1464d 随 proto 头入库;独立 138/0+官方 42B 对拍;task-4-report.md)。
- [x] Task3 fix re-review 盖章 Accepted(原审计 A ses_eeadef9eeffeZrXW8QG3luBj4T,commit ddf1464d 复验 I-1/I-2/7 Minor)。
- [x] Task4 独立评审有条件通过→修复 I-1+M-1 commit 2c807f7→fix re-review 盖章 Accepted(ses_eea065130ffecNVpYZFxX618Qu)。
- [x] Task10 完成(commit 832a226b:batch_strip/strip_matrix 6 门 64 组合、README 双语六门 64 矩阵、flatbuffers_demo 663 行、strip_matrix_64.txt 64 行断言表;主 55642/0+build-off 32778/0)。
- [x] Task11 最终双审全部 ACCEPT(审计A 产物正确性零缺陷 + 审计B 测试/剥离真实性,2 Important 遵主裁纯文档修复 commit 556c0e5)+ 一次性报告 + 深度验证目标 update_goal complete(2026-10-07)。
- 新功能阶段 11+1 全部闭合;遗留(记录不修):预存 Inner ODR、\u 代理对校验、包限定 extendee 名。

## 错别字勘误(2026-10-07)
- "完成后在合" 疑为 "完成后再做" 的笔误,用户原意=全部解决后一次性报告。

## 全量审计+非模板迁移阶段(2026-10-07 新目标:开启多个团队并行审计所有实现发现问题修复问题(真问题)还有必须把头文件中大量本该在源文件中的东西迁入源文件实现)
- [x] 并行只读审计团(分工完成):①核心链 2 Imp+2 Minor;②格式适配器 4 Imp+4 Minor+3 存疑;③proto/flat 家族 2 Critical+3 Important+4 Minor。全部带 file:line 证据 + 迁移候选清单。
- [x] 修复闭环(6 commit):M1 1eb270a(base_convert+naming)→F-B 3a4d409(ini/toml)→F-C 8ab32fb(xml/json)→F-A 9dedef8(core)→M2 72096c7(date_time+scan)→F-D ed2e813(proto/flat)。主代理终验 rtti_tests 55696/0、proto_ext 160/0、flat_schema 408/0、build-off 32790/0、0e0w。
- [x] M1 base_convert+naming + M2 date_time+scan 已迁 src;src/ 现有 13 cpp。
- [~] 双审复核 6 commit(复用审计A/B/C scoped re-review,已派发待回执)。盖章后派剩余迁移批次(json/xml/ini/toml 大户→src/{xml,json,ini,toml}.cpp、to_string text_node→src/text_node.cpp、proto_schema parser)。
- [x] 非模板迁移(用户硬性要求头中实现迁入 src/*.cpp;依据 docs/superpowers/notes/nontemplate-migration.md 批次 + 审计迁移清单):8 commit 全落地。Base A(base_convert+naming 1eb270a)→ 格式 doc/parser 外移(ini 6ca7cbf/json ac6b814/toml 17dd340/xml 72aaffb)→ scan/date_time(72096c7)/to_string text_node(12c03d13)/proto_schema parser(2bb796a4)。迁移后行为零变化=全量 55696/0+剥离 32790/0 回归 + cl 0e0w。
- [x] 验证基线(Debug 权威):主构建 rtti_tests=55696/0、剥离 build-off=32790/0、rtti_proto_extension_tests=160/0、rtti_flat_schema_tests=408/0;迁移与修复全部 commit 后统一收尾跑(主代理终验通过)。另 CMake 门控修复 ce67c6a(格式源条件追加)。
- [x] 收尾:一次性报告(真问题清单修复现状 + 迁移清单与落 src 现状)→ 全部完成。