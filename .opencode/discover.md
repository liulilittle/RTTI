# 失败档案

## 2026-10-09 文档编造 API 名(已修复+自动检查法)
- [尝试] zh/reflection.md §6 概述示例用 `attr_arg::make_int(3)`、`make_str("x")` 描述属性参数工厂。
- [错误] `attr_arg` 真实工厂为 `attr_arg::integer(uint64)`/`real(double)`/`text(string_view)`/`boolean(bool)`(reflection.hpp:879-898),并无 `make_int`;`make_str` 只是 tests/box.hpp 测试双的助手,非公开 API。接口文档示例引用编造/错位 API 名,事实性核查(提取反引号 `` `name(` `` 比对 include/rtti/)才暴露。
- [下一步] 文档示例与签名一律先 grep 头文件实证再写;提交前跑自动 API 名存在性检查(反引号标识符 + RTTI_* 宏全量比对),命中即修;勿凭记忆编造工厂/成员名。

## 2026-10-05
- [尝试] MSVC 空参检测:多版 IS_EMPTY(magic_enum 式/jbeder 式/Boost 精简式)反复失败。
- [错误] 通用空参检测宏在 MSVC 上不可靠;MSVC 传统预处理器 __VA_ARGS__ 展开有缺陷(需 /Zc:preprocessor);NARG 空参返回 1(空参数占位)与单参不可区分。
- [下一步] 采用 Boost PP is_begin_parens 语感:先测"参数后接 () 是否为括号"(IS_BEGIN_PARENS),CAT 间接粘合宏(两层间接 a##b)确保展开顺序;空参 `__VA_ARGS__ ()` → `()` → IBP=1 → 走空分支。已验证成功。域限制=反射字段成员名不会以括号开头,该受限 IS_EMPTY 可靠。

- [尝试] RTTI_DEFINE_TYPE 在类体内使用。
- [错误] RTTI_DEFINE_TYPE 是外部注册宏,展开 `namespace rtti {...}` 模板特化,放在类体内报 C2059 语法错误。
- [下一步] 类体注册必须用 RTTI_DEFINE_TYPE_INTRUSIVE / _INTRUSIVE_WITH_DEFAULT(定义嵌套 rtti_registration)。

- [尝试] `o.*ptr.size()` 写容器成员方法调用。
- [错误] C++ 运算符优先级:`o.*ptr.size()` 解析为 `o.*(ptr.size())`,`.*` 优先级低于 `.`,报 C2228。
- [下一步] 成员指针解引用后调方法必须加括号 `(o.*ptr).size()`。

- [尝试] 容器 shape trait 定义放在 typed_field_access 之后。
- [错误] MSVC 即使依赖模板参数也要求被引用模板名在定义点可见——报 C2065 is_map 未声明/C3878。
- [下一步] 所有 trait 定义移到使用它们的模板之前(核心头前部)。

- [尝试] type_of<T> 用递归 static type_desc 构造(自引用结构体 SelfRef 内嵌 vector<SelfRef>)。
- [错误] 自引用会无限递归 static 构造(编译失败或死锁),不可靠。
- [下一步] 字段/容器元素类型改用"解析函数指针"(type_resolver)惰性访问:type_of 构造期零递归,访问时才解析已构造完成的 static。

- [尝试] for_each 容器遍历回调用 C 风格函数指针 + void* ctx + 局部 struct。
- [错误] 用户明确批评"使用标准接口不要弄莫名其妙的东西"。
- [下一步] 回调用 std::function(element_visitor);add_field 提供 field_value(标量)与 value_io*(通用)双重载。

- [尝试] 各格式适配器(JSON/XML/INI)头内各自定义枚举转换(JSON converter 特化、XML/INI 3 参 ADL 钩子,均在 namespace rtti)。
- [错误] 同 TU 同时 include 三个格式头时枚举转换冲突/分派歧义;XML 无条件 is_enum 钩子对未注册枚举炸编译。
- [下一步] 枚举↔文本属核心能力(enum_table 在 reflection.hpp),统一移入 convert.hpp:converter<T, enable_if<is_enum && is_registered_enum<T>>> 特化(带守卫);格式头不再定义枚举转换。

- [尝试] 多个测试文件独立定义同名全局枚举(如 xml_test 与 type_desc_test 均全局 enum class Color;json_test 与 macro_test 均 rtti_test_lib::Color)。
- [错误] 单独编译各 TU 全绿,链接成一个 exe 后跨 TU 同名类型 ODR 违规 → enum_table 特化行为未定义 → 运行时枚举转换静默失败。
- [下一步] 测试枚举必须跨 TU 唯一命名(JsonColor/XmlColor/IniColor),或在唯一命名空间内;禁止多个测试 TU 共享同名全局/同命名空间类型。

- [尝试] tests/CMakeLists 无条件加入三个格式测试源。
- [错误] 剥离构建(三门全关)下格式测试 include 格式头触发 #error C1189。
- [下一步] 格式测试源用 if(RTTI_ENABLE_*) 条件追加,剥离构建自动排除。

- [尝试] PowerShell 内联脚本给 CMake 传开关:`-DRTTI_ENABLE_JSON=$c.j`、`-DRTTI_ENABLE_XML=$c.j`(foreach 循环)。
- [错误] 双引号内 `$c.j` 未展开为属性访问,CMakeCache.txt 收到字面 `$c`,CMake 将其判为非空真值 → 三门全开 → 剥离矩阵八组合全部显示 1245 assertions(本应 core=607),八组合验证整体无效,检查 CMakeCache 才暴露。
- [下一步] PowerShell 变量属性在字符串内必须用 `$($c.j)`(子表达式);配置后必须读 CMakeCache.txt 核验 RTTI_ENABLE_* 实际值,禁止仅凭测试断言数判断门是否生效。

## 2026-10-07 Task5 反复救护(已解决:Task5 已交付闭合;教训沉淀)
- [尝试] Task5(flat wire)子代理连续 4 次救护,均因大文件单次 Write >8KB 在超时窗口未完成被中止;获救后无进度 relay 支撑。
- [错误] 子代理用单次 Write 写大文件(>8KB)超时中止;救护恢复后无心跳导致主代理误判存活。
- [下一步] 大文件写必须分小块(Write ≤6000B / PowerShell Add-Content 分批),每段后 relay 进度;连续救护会话每次恢复必须先要求立即 relay 心跳+再派小步命令,禁止一次性长任务。
- [2026-10-09 复发] 主代理单次 Write 写 ~34KB reflect-tools.md 亦被中止(会话停滞救护)。[下一步] 文档大文件同样分写:先 write 首段(~5KB),再多次 edit 追加小段,每段成功后再续;已完成(EN/ZH 各 ~590/645 行,全部 edit 成功)。

## 2026-10-07 Task8a 复评"误判回归"教训
- [尝试] 复评员在共享工作树跑全量构建验证基线。
- [错误] 实测 55642/9 初看像回归,实为并行 Task3 修复在途未提交 WIP 编入 exe(proto.hpp/proto_schema.hpp/proto_schema_test.cpp 均 M)。
- [下一步] 并行 WIP 期禁止按共享工作树断言数判定回归;必须独立构建(scratch/%TEMP%)各自红绿,全量断言留全部 commit 后统一收尾;子代理报告与父基线不符如实报告并归因(路径证据),禁止掩盖。

## 2026-10-09 Task4 中断恢复 + 两处承重教训
- [尝试] 主代理在子代理救护中断后直接 `cmake --build build`。
- [错误] C1041「无法打开 rtti.pdb」— 中断子代理遗留的孤儿 cl.exe(PID 26560,启动 11:20,挂 13.5 分钟 CPU 仅 5.3s)仍占用 PDB。非代码错误。
- [下一步] 子代理中断后先 `Get-Process cl,MSBuild` 查孤儿编译器;僵死(CPU 停滞/超 5 分钟)用 Stop-Process 清理后再构建;勿误判为代码缺陷。
- [尝试] `class instance` 成员 `as<T>()` 内写 `type_of<T>()`(非模板)。
- [错误] C3878 级联语法错误 @reflection.hpp:2777 — `class instance` 自身有非模板成员 `type_of()`(@2708),在成员作用域内**遮蔽**自由函数模板 `rtti::type_of<T>()`;`type_of<T>()` 被解析为「成员 + 小于号」。
- [下一步] 类内调用被同名成员遮蔽的自由函数模板必须显式限定 `rtti::type_of<T>()`(或改用 `detail::type_of_impl<T>()`);同类风险:`type_of`/`as`/`cast` 等自由函数与成员同名时。修复者须在成员函数内限定命名空间。
