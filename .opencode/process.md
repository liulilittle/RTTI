# 进度

## 2026-10-05 会话
- Spec 定稿:反射核心 + JSON/XML/INI 可剥离适配器(项目名 RTTI,命名空间 rtti)
- 计划定稿:12 个 TDD 任务(骨架→error/arena→字段表→宏族→convert→递归/ADL→运行时API→box测试双→剥离矩阵→双语文档→基准)
- 工具链: MSVC 2022 Enterprise(含 CMake);无 GCC/Clang/cmake 在 PATH

## 已完成
- [x] Task 1: 骨架/CMake/测试框架/rtti 命名空间。测试通过(5, 0)。
- [x] Task 2: error_code/error_info/result<T>。测试通过(19, 0)。
- [x] Task 3: string_view + MemoryArena。测试通过(30, 0)。
- [x] Task 4: type_id/field_value/field_access/fields_of。测试通过(48, 0)。
- [x] Task 5: RTTI_DEFINE_TYPE 宏族+RTTI_DEFINE_ENUM。测试通过(63, 0)。要点:显式特化须在全局/rtti 命名空间;侵入宏用嵌套 rtti_registration;MSVC 需 /Zc:preprocessor;NARG 单层。
- [x] Task 6: value_io + convert<T> 标量/string/四容器。测试通过(108, 0)。要点:std::string 排除于 is_sequence;make_node() 建子节点;const at/get_key/get_key_at;返回 error_code;stderr 输出。
- [x] Task 7: 嵌套递归 + ADL 覆盖 + 默认键。测试通过(126, 0)。要点:field_access_base 增 get_value/set_value;reflection.hpp 前向声明 from/to_value;box.hpp 共享;文件被 VS Code 锁时经 memory.hpp 间接 include error.hpp;PDB 冲突杀 cl/mspdbsrv。
- [x] Task 8: 运行时查询 API(reflect.hpp:type_info/get_type/find_field/get_field/set_field)。测试通过(141, 0)。要点:类外模板成员函数定义在 MSVC 报 C3878 解析错——find_field 改类内 inline 定义规避。
- [x] Task 9: Review Focus 用例(未注册类型/空结构体/自定义 alloc 容器/严格 vs 默认缺键/容器内 ADL)。测试通过(164, 0)。要点:RTTI_IS_EMPTY 用 Boost is_begin_parens 语感(CAT 间接粘合避免 MSVC 直接 ## 未展开);空参 count=0 用 (RTTI_IS_EMPTY ? 0 : RTTI_NARG);RTTI_APPLY_OPT 空参不展开;类体注册必须用 INTRUSIVE 宏(RTTI_DEFINE_TYPE 非侵入在类内展开 namespace 报 C2059)。
- [x] Task 10: 剥离矩阵 + 格式门 stub。双构建全绿(主 build 166/0;build-off all-off 166/0)。要点:json/xml/ini.hpp gate stub(#error 当门关);strip_matrix_test.cpp 源文件层强制关三门证明核心零格式头依赖;reflection_smoke 的 feature_gates_default_on 改为 feature_gates_defined(值 0 或 1),兼容剥离构建。
- [x] Task 11: 中英双语 README(README.md 导航 + README.en.md / README.zh-CN.md,章节互索引)。
- [x] Task 12: 反射基准(bench_timer.hpp RAII steady_clock;bench_reflect.cpp field_walk/get_set_by_name/reflect_roundtrip ops/s)。实测:field_walk ~180M ops/s,get_set_by_name ~24M,reflect_roundtrip ~63k(box 树分配拖慢,合理)。

## 全任务完成
- 12 个任务全部完成;主线 build 与 build-off 双构建测试均 166 assertions 0 failures。

## 2026-10-05 扩展(容器操作 + 适配器 + 类型描述)
- [x] 适配器家族:is_queue/is_stack/is_priority_queue shape 检测 + queue/stack/pq 的 from/to 转换(顺序语义:queue FIFO、stack LIFO 反序重建、priority_queue 堆序重建)。注意 priority_queue 必须先于 is_stack 分派(它也有 push/pop/top)。
- [x] 运行时容器操作:field_access_base 增加 is_container/field_add/field_add_scalar/erase_at/erase_key/clear/size/for_each 虚方法;typed_field_access 按容器类型实现;reflect.hpp 提供 add_field(value_io*/field_value 双重载)/erase_field_at/erase_field_key/clear_field/field_size(error_code& 可空)/for_each_element。元素遍历用调用方提供的 scratch value_io 承载。
- [x] 类型描述系统:type_desc{kind,id,type_name,container/reflected/enum} + type_of<T>() 惰性 static;字段/元素类型用 resolver 函数指针延迟解析(自引用结构体不递归爆炸);RTTI_DEFINE_TYPE 宏扩展生成 desc 字段类型数组;enum 经 enum_table 贯通。
- 双构建全绿(329, 0),零警告;bench 正常。
- 架构调整:value_io 从 convert.hpp 移到 reflection.hpp(核心头,因 field_access_base 容器操作需调用它);容器 shape trait/is_reflected/key_traits 也从 convert.hpp 移到 reflection.hpp(核心特性);convert.hpp 仅保留转换实现。用户批评过"用标准接口不要弄莫名其妙的东西"——for_each 回调改用 std::function(element_visitor)而非 void* ctx 函数指针,add_field 补 field_value 标量重载。

## 工程实现阶段(用户需求 3:JSON/XML/INI 格式适配器)
- [x] JSON 适配器(include/rtti/json.hpp + tests/json_test.cpp,子代理实现,主代理独立核验 200/0 零警告)。DOM 自包含 arena、递归下降 parse、strict 转义、sort_keys、深度上限 512、parse_as<T>/serialize<T> 反射桥接、枚举经核心 converter 特化。
- [x] XML 适配器(include/rtti/xml.hpp + tests/xml_test.cpp,188/0 零警告)。元素/属性/文本/混合内容/CDATA/注释/PI/实体(预定义+数字+DTD 内部自定义,深度 32/次数 10000 防 Billion Laughs)、命名空间(xmlns 原样+ns_uri 沿祖先查)、value_io 映射(属性优先→_text 特判→同名子元素/重复子元素 array 集合)。
- [x] INI 适配器(include/rtti/ini.hpp + tests/ini_test.cpp,修复后 160/0 零警告)。节/键值(= 与 : 分隔)/注释 ;#/引号值转义/重复键后者覆盖、重复节 array 映射、嵌套节点路径、反射桥接。
- 主构建 877/0;build-off(三门全关)329/0,零警告。
- 关键集成修复(主代理):① 枚举转换原在各格式头各自定义(JSON converter 特化 / XML/INI 3 参 ADL 钩子)→ 三头共存 TU 冲突;统一移入 convert.hpp 核心(converter 特化 + is_registered_enum 守卫),格式头不再定义枚举转换。② 跨 TU 枚举重名 ODR 冲突:json_test/macro_test 均在 rtti_test_lib 定义 Color、xml_test/type_desc_test 均全局 Color → 链接同 exe 行为未定义;改名隔离(JsonColor/XmlColor/IniColor)。③ tests/CMakeLists 按 RTTI_ENABLE_* 条件追加格式测试源,剥离构建自动排除。
- 可优化方向(子代理回报,未实现):SIMD 结构字符扫描换入点、arena/节点池复用、对象键哈希、单元素/空序列在扁平 INI 的编码扩展、序列化缓冲预分配、零拷贝 parse 变体、XML 命名空间作用域栈、map 含 vector 的 XML 映射包装。

## 收尾阶段(补齐 spec 缺口)
- [x] 流式 SAX API(§7.2):JSON parse_events(对象/数组/key/标量/null 回调)、XML(元素/属性/文本合并/CDATA/注释/PI)、INI(section/key_value),parse_stream(src, events) 返回 false 取消、错误返回 error_info。
- [x] serialize(root, buffer) 预分配入口(§7.1):三格式 serialize(doc, std::string& out, cfg),追加语义(重复调用拼接,覆盖需 clear)。
- [x] 剥离矩阵八组合(§8):core 329/0、core+JSON 561/0、core+XML 554/0、core+INI 510/0、core+JSON+XML 786/0、core+JSON+INI 742/0、core+XML+INI 735/0、全开 967/0,全部零失败零警告。
- 主构建(全开)967/0;三 SAX 独立核验:JSON 232/0、XML 225/0、INI 181/0。
- 六个子代理(三格式实现 + 三格式 SAX)均经主代理独立编译核验后解散。

## 性能优化阶段(用户选定:SIMD 结构字符扫描 + MemoryArena 节点池复用)
- [x] P1 共享扫描工具 include/rtti/detail/scan.hpp(子代理,主代理独立核验):128 位 char_mask{lo,hi}(JSON 结构字符 { } [ ] ASCII 91–125 超 64 位掩码范围,故不用 uint64);SSE2(MSVC _M_X64/_M_IX86 无条件、GCC/Clang __SSE2__)+ 标量回退(其他平台/DRTTI_DETAIL_DISABLE_SIMD);find_first_of/find_first_not_of/skip_whitespace/is_structural_or_string/json_structural_mask/xml_special_mask/whitespace_mask,全 noexcept。SSE2 与标量双路径各 278/0 零警告,主代理复验一致。scan_test.cpp 已加入 tests/CMakeLists(RTTI_TEST_SOURCES 无条件,核心测试);harness 静态自注册,test_main 无需改动。
- [x] P2 JSON/XML 节点池改造(子代理 ses_ef405df0fffey7m3fnETS2xY4o,主代理核验通过):owned_ 改裸指针 vector,节点本体 arena alloc_obj 分配,显式 ~node() 级联析构 + destroy_nodes(),move 语义保持;消除每节点 new/delete。主构建 1245/0(含 scan)零警告。bench/bench_parse.cpp 独立基准(warm-up+checksum+30iters),JSON +46% / XML +24% 吞吐,checksum 一致证明语义零变化。
- [x] P3 格式解析器换入 scan.hpp(子代理 ses_ef3e29760ffe7sE2zlib2xvX7L,主代理核验):json/xml 的 skip_ws 换 skip_whitespace(带短段直扫快路径);xml 顶层文本/属性值/实体扫描换 find_first_of;未换点(JSON scan_string 引号/转义、XML 文本、decode_string、skip_declaration、find_substr 多字节、read_name)保留原文+注释(scan 不合适:掩码无法表达多字节序列/需精确 span)。门控≥16B 触发 SIMD。error_offset 语义不变。
- [x] P4 基准:主代理复验 json 6.4MB/s / xml 2.7MB/s(Debug),checksum 341910/314550 前后一致;节点池 JSON +46% / XML +24%;SIMD 换入在紧凑短令牌基准持平。
- [x] P5 回归:主构建 1245/0 零警告;build-off(core)607/0(329 核心+278 scan);剥离矩阵八组合 607/839/832/788/1064/1020/1013/1245 全绿。**教训:内联 PowerShell 双引号内 $c.j 不展开(CMakeCache 收到字面 $c),剥离矩阵脚本必须用 $($c.j) 或脚本文件传参**。
- 三个性能优化子代理(scan/节点池/换入)均经主代理独立编译核验后解散。

## 性能与易用性强化阶段(用户新指令:allocator/less + 更强性能 + 更强易用性;用户强调 modern C++=保持 C++17)
### 2026-10-06 已完成
- [x] (主力)三格式 SAX + serialize(buffer) + 剥离矩阵八组合 + SIMD scan + arena 节点池,主构建 1245/0 零警告(上下文中已有详细记录)
- [x] brainstorming spec 定稿:docs/superpowers/specs/2026-10-06-perf-usability-design.md(A allocator/less、B 新类型、C 性能+bug、D 配置四节,用户批准)
### 探针事实(硬证据)
- 自定义 allocator/less 现状:自定义 less map ✓、自定义 allocator vector/map/键 ✓;**自定义 allocator 的 basic_string ✗ 被当字符数组序列化 [104,105]**(根因 is_sequence 仅排除 std::string/wstring)。
- 真实 bug 1:XML 标量根序列化丢值(parse 后 serialize 仅出声明);INI make_scalar(3.14) serialize 空输出。
- 真实 bug 2:XML/INI 浮点 3.14→3.1400000000000001(JSON 为 3.14),格式化路径不一致。
- JSON get_key 为线性扫描(可哈希索引优化)。
- B 节可行性:is_optional/is_variant/is_tuple/运行时索引 variant 分派(float)、to_chars(3.14)='3.14'、snake 转换全部可行。
- **坑(MSVC)**:std::apply(index_sequence) 不可靠(tuple_size C2131);std::array 含 tuple_size 特化,is_tuple 判别会误伤 array;variant fold 展开 .size() 对所有 alternate 实例化,须 variant_alternative_t+if constexpr 隔离。
### 下一步
- [ ] 修正 spec:is_fixed_seq 判别不能用 is_tuple 排除 array(4.6/4.7 实施细节)
- [ ] writing-plans 出实施计划
- [ ] 执行四节 + 验收

## 执行阶段(SDD,计划 docs/superpowers/plans/2026-10-06-perf-usability.md)
- [x] Task A1(子代理 ses_ef29d7c3cffeezn33eIGkMwWna,主代理独立核验):is_char_string trait(reflection.hpp:47-50)部分特化匹配任意 basic_string;is_sequence enable_if 改 !is_char_string(reflection.hpp:59)。tests/custom_alloc_test.cpp 新建(ProbeAlloc/ProbeStr/rtti_test_lib 独立命名)。独立编译 1/0 零警告;全量回归 1245/0。注:子代理误在根目录建 process.md(按计划 Step 5 字面),已删除并归并本文件。