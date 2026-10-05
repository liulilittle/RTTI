# 方案

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