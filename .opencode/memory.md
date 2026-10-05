# 跨项目复用记忆

## 环境
- 平台: win32,PowerShell 5.1,非 git 仓库
- 工作目录: F:\json_xml
- 工具链: MSVC 2022 Enterprise(含 CMake,无 GCC/Clang 在 PATH);CMake 路径 "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe";生成器 "Visual Studio 17 2022" -A x64

## 本项目经验
- MSVC 传统预处理器 __VA_ARGS__ 展开有缺陷:宏族必须加 /Zc:preprocessor(标准预处理器),否则 NARG 类计数错误。
- MSVC 空参 `NARG()` 返回 1(空 __VA_ARGS__ 当作 1 个空参数占位),与单参不可区分;通用 IS_EMPTY 宏在 MSVC 不可靠。Boost PP 方案:IS_BEGIN_PARENS(检测参数后接 () 是否为括号)+ 两级间接粘合(CAT_I/CAT_II 确保 a##b 前先展开),域受限(字段名不以括号开头)时可靠。
- 显式模板特化必须在全局或目标命名空间声明,匿名命名空间内特化非法([temp.expl.spec]);类体内不能写模板特化——侵入式宏用嵌套 rtti_registration 结构让 fields_of 继承。
- 模板成员函数的类外定义在 MSVC 报 C3878/解析错——改类内 inline 定义规避。
- 静态注册期测试用例崩溃不可见:harness 输出走 stderr(fprintf)无缓冲;main 用 setvbuf(_IONBF)。
- MSVC PDB 冲突 C1041:构建失败时检查并杀残留 cl.exe/mspdbsrv.exe 进程。
- Windows 文件排他锁:VS Code(Code.exe)打开文件时 edit/write 持续 Busy;可绕开(经另一头文件间接 include 目标),或等待释放;写前可用 System.IO.File::Open(FileShare::None)探测锁。
- 测试双(box)共用头 tests/box.hpp:value_io 的内存实现 + make_* inline 工厂;make_scalar 需复制字符串到 owned 成员,否则容器适配器转换后 view 悬垂。
- C++ 运算符优先级:`o.*ptr.size()` 解析为 `o.*(ptr.size())`——成员函数调用须加括号 `(o.*ptr).size()`。
- 模板虚方法体内的 trait 依赖:MSVC 要求被引用模板名在定义点之前可见(即使依赖 M),所以容器 shape trait 必须放在 typed_field_access 之前。
- 自引用类型描述(type_of<SelfRef>)不能直接递归 static 构造——字段/元素类型用"解析函数指针"惰性访问,构造期零递归。
- 标准接口偏好(用户明确批评过):回调优先 std::function 而非 C 风格 void* ctx 函数指针;公开 API 提供标量(field_value)与通用(value_io*)双重载;错误传播用可空 error_code* 参数。
- 枚举↔字符串转换是核心能力(enum_table 在 reflection.hpp):应在 convert.hpp 统一提供 converter 特化(带 is_registered_enum 守卫),禁止各格式头各自定义枚举转换(同 TU 多格式共存冲突)。
- 跨 TU 链接陷阱:多个测试 TU 定义同名全局/同命名空间类型(枚举/结构体)会 ODR 违规,单独编译全绿但链接成一个 exe 后行为未定义(静默失败)。测试类型必须跨 TU 唯一命名。
- 剥离构建:格式测试源必须用 if(RTTI_ENABLE_*) 条件追加,否则三门全关时格式测试 include 触发门 #error。
- MSVC 独立编译链接:裸 cl 默认 /MT Release 与 Debug rtti.lib(/MDd)不匹配(LNK2038);须加 /MDd;/LIBPATH 必须置于 /link 之后;需 MSVC lib\x64 + UCRT Lib + um Lib 三个 LIBPATH(无 VS 环境)。
- 子代理零产出报完成是无效完成声明:必须核验产物(文件大小/编译运行证据);报告与实况不符(如声称 0 failures 实为 7 failures)必须下重处理令,根因可能涉及未定义行为(如 SSO 短字符串悬垂,Release 侥幸通过/Debug 暴露)。
- SIMD 结构扫描实践:128 位 char_mask{lo,hi}(JSON 结构字符 { } [ ] ASCII 91–125 超出 64 位范围,uint64 物理无法表示);SSE2 用 _mm_loadu_si128+_mm_cmpeq_epi8+_mm_movemask_epi8,尾部<16B 标量收尾,永不越界读;全平台回退用 /DRTTI_DETAIL_DISABLE_SIMD 强制标量验证双路径一致性。
- SIMD 门控经验:紧凑短令牌文档(<16B 连续扫描段)SIMD 无收益且掩码构造/collect_targets 有开销——必须带"短段直扫+≥16B 走 SIMD"门控,并保留个别最热短扫描(JSON 引号/转义、XML 文本)为原循环,否则回退 >20%。
- Arena 节点池改造:节点本体从每节点 new 改为 memory_arena alloc_obj + owned_ 裸指针 vector + 显式 ~node() 级联析构 destroy_nodes();arena 不自动析构,vector 成员(children/attrs/strings)仍需析构器;move 语义需回填 arena/owner 背指;字符串仍走 arena dup。
- PowerShell 内联 CMake 传参陷阱:双引号内 $c.j 不展开为属性,CMakeCache 收到字面 "$c" 使开关判定为真(三门全开)——剥离矩阵/开关传参必须用 $($c.j) 或脚本文件,配置后必须读 CMakeCache.txt 核验 RTTI_ENABLE_* 实际值。
