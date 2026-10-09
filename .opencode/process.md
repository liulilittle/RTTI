# 进度

## 当前目标(2026-10-08,active):继续深度研究优化(注释/文档专业详细+缺口全修)
- 用户指令(create_goal):「继续深度研究优化:确保绝对无问题、绝对好,注释、文档写得非常专业、详细,严格遵循插件系统提示词要求」。
- spec docs/superpowers/specs/2026-10-08-migration-verify-comments-docs-gaps-design.md(commit 0152550)+ plan docs/superpowers/plans/2026-10-08-post-migration-deep-verify.md(commit 6f06af1)。用户确认:四项全选(二次审计/注释/文档/缺口)+ 六缺口全修 + 方案A(分层团队双审)+ Subagent-driven。
- **链 commit 9 个已全部落地与双审 ACCEPT**:7a3ddc5(Task2 注释 10 项)/a63ab1b(Task3 文档镜像)/ce6312c(G-2 enum int32)/dd5e623(G-1 作用域 extension)/f23da97(G-5 包限定 extendee)/4b0f533(Task6/7 测试锁定 +47)/92291da(C9 number_format 同型注释)。上游:0152550(spec)/6f06af1(plan)。
- **工作流闭环**:Task1 三审计团二次审计(全 ACCEPT)→波次1 三实现组(Task2/3/缺口组)+Task6/7 降级验证+Task2/3 task review→C9 同型修复→scoped 三审计复核全 ACCEPT→统一终验。
- **最终基线(Debug 权威)**:主 rtti_tests **55774/0**、proto_ext 160/0、flat_schema 408/0、剥离 build-off **32800/0**(基线 32790,Δ+10=format_test.cpp 无条件编入的代理对断言),双构建 0e0w,工作树干净(仅 .opencode+历史未跟踪 docs)。
- W6 验收全部[完成]见下节;无遗留真缺陷;子代理会话全部 delete 解散。

### 本目标 W6 验收(2026-10-08)
- [完成] 二次深度审计:Task1 三审计团(核心链/格式/proto-flat)只读复核 19 cpp+22 头正确性零缺陷 + 注释/文档过期清单 + 缺口实证。证据:process.md Task1 聚合段+task-1-audit{ABC}-report.md。[READ:process.md]
- [完成] 注释专业补强(Task2 7a3ddc5):10 项全修(error.hpp:12 适配器枚举/convert.hpp 现状/ioc §18/rtti_fwd 宏数/string_ops surrogate/reflect.hpp 语法/common/format 缩进)+G-6 toml 措辞;review 10/10 + 审计A/B 盖章。证据:git show 7a3ddc5。[BASH:git log]
- [完成] 文档体系(C9 同型后 Task3 a63ab1b):README en/zh 镜像(断言 55696/32790/160/408 全刷,旧值零命中,header-only 表述改 src 静态库 19 TU)+migration 笔记快照横幅。证据:git show a63ab1b + 审计B README.en:302-306/528。
- [完成] 六缺口:G-1 作用域 extension(dd5e623)/G-2 enum int32(ce6312c)/G-5 包限定 extendee(f23da97)功能性修复 TDD 红绿;G-3 flat u64+G-4 代理对已实现→降级锁定 4b0f533;G-6 toml 措辞入 Task2。证据:task-4-5-8/report+auditC 复核。
- [完成] C9 同型扫描:审计B 六处 never-throws,裁定 4 处非同类,number_format.hpp:141/150 同型修复 92291da。证据:git show 92291da。
- [完成] 终验:主 55774/0+proto 160/0+flat 408/0+剥离 32800/0+0e0w。证据:[BASH:rtti_tests/build-off]。

## 已归档目标(complete):全量审计(真问题修复)+ 非模板实现迁入 src
- 用户指令(create_goal):「开启多个团队并行审计所有实现发现问题修复问题(真问题)还有必须把头文件中大量本该在源文件中的东西迁入源文件实现。」补充强调:**该迁入源文件的实现必须迁入,禁止继续积压头文件(编译体积无限膨胀),迁入 src 为硬性要求**。(已 complete,commit 链与证据保留下文)
- 现状:HEAD=556c0e5 工作树干净;include/rtti/ 22 头(含 detail/scan.hpp)+ src/ 9 cpp;迁移评估文档 docs/superpowers/notes/nontemplate-migration.md(Band A=base_convert+naming 零风险;格式 doc/parser 大批次;scan 中;proto_schema parser 强)。
- 已派发 4 并行子代理(后台,r28 详列):审计A 核心链(ses_ee88100c9ffe4crn8u1URRXbhs)/审计B 格式(ses_ee880e45affejW0bHESG534pbO)/审计C proto-flat(ses_ee880bb4bffeMtpRm0SE8y8coY)/M1 迁移 base_convert+naming(ses_ee8809d14ffegS9TC2OwH7M2XE)。
- **M1 已完成并主代理独立复验(commit 1eb270a,+300/-226,5 文件:base_convert.hpp 12 函数改声明/naming.hpp 3 函数改声明/src/base_convert.cpp+src/naming.cpp 新/CMakeLists 追加)。[BASH 复验] 主三重套件 55642/0+160/0+401/0 与基线完全一致;build-off 32778/0。行为零变化(纯剪贴,默认实参留头定义去重)。M1 闭环。**

## 审计A/B 报告聚合(2026-10-08,真问题清单→派修复)
### 审计B 格式适配器(ses_ee880e45affejW0bHESG534pbO)真问题
- [Imp] ini.hpp:188-195/207-214 as_integer/as_real 漏 errno==ERANGE 检查(9223372036854775808→静默 INT64_MAX;1e999→inf);xml/toml 均查 ERANGE,ini 唯一漏检,无测试覆盖。
- [Imp] xml.hpp:1251-1274 数字字符引用 cp=cp*16u+d 无上界,`&#x100000041;` 回绕成 0x41 'A' 通过校验输出(应 bad_entity);无测试。
- [Imp] xml.hpp:525-527 注释宣称 "Owns its entire tree" 但实现零拷贝 string_view 指向 parse 源缓冲——临时 std::string 解析后保留 document 读文本→悬垂(与 json.hpp:20-23/ini/toml 全复制契约冲突)。四格式生命周期语义不一致。
- [Imp] serialize(doc, out, cfg) 同名同签名语义四格式不同:json/ini/toml clear 覆盖 vs xml append——跨格式复用缓冲静默差异。
- [Minor×4] json/xml new_node 未判空(OOM,malloc 可返 null 但 memory.hpp:40 注释承诺"仅 size 0 返 null"→契约不符);四格式序列化器写侧无深度上限(parse 侧有 512,程序化构造深层文档 serialize 栈溢);toml.hpp:1504-1507 inline_table 内 too_deep 被 unexpected_token 覆盖;xml.hpp:1616-1622 read_name 高字节名注释与实现矛盾。
- [Minor-存疑×3] 重复键读取语义跨格式不一致(json/xml first-match,ini/toml last-wins);xml vs ini as_integer 空白容忍差异;xml merged_text 惰性缓存 stale。
- 无发现(已核查全过):四格式深度上限 parse 侧全覆盖、整型/长度溢出、UTF-8 代理对、ODR、scan SIMD 双路径、XML 实体递归双限、JSON 转义。

### 审计A 核心链(ses_ee88100c9ffe4crn8u1URRXbhs)真问题
- [Imp] src/memory.cpp:87-111 memory_arena::alloc 跨块分配时对齐 pad 残留(触发新块分配后 cursor_ 换新对齐地址,旧 pad 叠加错 0..align-1);对 double/16B 对齐属 UB;memory_test 只测首块内未覆盖。
- [Imp] convert.hpp:1090-1106 fixed_sequence_from(std::array) 静默截断/留旧尾 vs carray_from 的严格 kind_mismatch(convert.hpp:1108-1114+static_array_test:297-302)—§0 静默错误。
- [Minor] reflect.hpp:770 element_iterator::key() set<real> 键用 std::to_string(6 位,0.1→"0.100000");ioc.hpp:280-282 借用句柄 destroy() 文档范围比实现窄(容器先亡则 UAF,misuse 边界)。
- 无 Critical。已核查无问题:error/to_string 28 码、base_convert 溢出/边界、reflect.cpp locate 全路径、迭代器边界、convert 各容器、to_string 压缩 JSON 解析器、number_format 语法全分支、date_time ±int64 边界、ODR/inline、宏 MSVC(/Zc:preprocessor+RTTI_IS_EMPTY)。

### 迁移候选(审计/评估交叉确认;详见 nontemplate-migration.md + 两审计)
- 高:xml.hpp(~1750 行 97%)→src/xml.cpp、json.hpp(~1090/91%)→src/json.cpp、toml.hpp(~1670/96%)→src/toml.cpp、ini.hpp(~1050/94%)→src/ini.cpp;中-高:scan.hpp 主体→src/scan.cpp;date_time.hpp(~470/81%)→src/date_time.cpp;to_string.hpp text_node+parser(~560/48%)→src/text_node.cpp;convert.hpp 非模板桥→src/convert.cpp。
- 模板残留仅各头 parse_as<T>/serialize<T> 壳与 fire 微型模板。迁移约束(审计明确):XML 保持零拷贝禁止顺手改自持;buffer 语义差异禁止"顺手统一"需单独立项;ini/toml pool_alloc(new→抛)vs json/xml arena(null)分配语义迁移时核对;vtable 在关键函数(dtor)TU 发射。
- 迁移时机已不阻塞(四头+scan 已全部提交 HEAD=1eb270a,git diff 空)。

### 修复/迁移派发(2026-10-08)
- [在途] F-A 核心修复(ses_ee841045dffei3pelW8yLeJrFt):A-1 arena 跨块对齐 pad 残留(UB)/A-2 fixed_sequence_from std::array 静默截断/A-3 set key to_string 精度/A-4 ioc 借用句柄文档。TDD 红绿。
- [在途] F-B ini/toml(ses_ee840ccfdffeom3i7HaGRKiYIx):B-1 ini ERANGE 漏检(静默钳位/Inf)/B-2 toml inline_table too_deep 覆盖。TDD 红绿。
- [在途] F-C xml/json(ses_ee8409036ffeSqe4NIH4NMMFrV):C-1 xml 数字实体回绕无上界(静默错误)/C-2 xml zero-copy 契约注释与实现矛盾(悬垂风险)/C-3 serialize(out) buffer 语义统一覆盖/xml + 四格式一致/C-4 json new_node OOM 未判空。TDD 红绿。
- [在途] M2 迁移(ses_ee840468cffekgG49XUkBgCF1u):date_time.hpp→src/date_time.cpp(470 行类方法)+ scan.hpp→src/scan.cpp(find_* 主体+SSE2 段);纯剪贴零逻辑变化;基线 55642/0+32778/0 不变。
### 审计C proto/flat 家族报告(ses_ee880bb4bffeMtpRm0SE8y8coY,只读,官方 wire 对拍逐个核验通过:basic.Scalars/Outer/advanced.All/ext.Base 十六进制与 monster.bin 均一致,无 wire 不兼容)
- [Critical] src/flat_schema.cpp:1590-1600 resolve_schema union id 校验 `f.id==0||f.id>total` 漏 `f.id==total` → `seen[f.id]`(vector<bool>,seen.size()==total)越界读写 UB/堆破坏,违反 §0。复现:`namespace t; union U { A; } table T { u:U (id: 2); }`(total=2,f.id=2);parse_schema:1638 即触发,public 直通。修复 `f.id>=total`(对齐 :1799)。
- [Important] proto_schema.hpp:1770 parse_enum `next_default=v+1` 有符号溢出 UB(`A=9223372036854775807; B`);flat_schema.cpp:1212-1214 已有守卫不一致。修复复用守卫或 int32 范围校验报错。
- [Important] proto_schema.hpp:1895-1910 类型引用按全局 simple-name/点后缀解析忽略作用域(iterator resolve 栈未传字段所属消息),命名遮蔽的合法 .proto 静默解错(示例 A.Inner/B.Inner 遮蔽)。修复:字段所属消息 full_name 自最深作用域向上解析。
- [Important] src/flat.cpp:553-561 u64>INT64_MAX 解码钳制破坏无损往返,违反 flat.hpp:34-35 承诺;typed 路径 parse_as<T> 必经 tree→uint64 成员静默损坏。修复:文档化限制或错误返回/保真。
- [Minor] flat_schema.cpp:176-207 实数默认字面量仅整数指数("1e3"),"1.5e3" 误拆;1445 union 非法引用错误 offset 陈旧;182/proto_schema:331 strtod 受 LC_NUMERIC 影响(存疑);proto oneof 标量 0 typed 不编码 vs protoc 编码 18 00(存疑,设计局限);proto_schema:1773-1774 enum 超 int32 双重截断(应 fits_int32 校验);scanner 不支持 hex/oct/指数整数字面量(存疑,subsmt 文档)。
- 迁移候选:proto_schema.hpp parser+scanner+resolve 全量外移(~2400 行/80%)→src/proto_schema.cpp(最高);proto.hpp 非模板 wire/tree/extension/document(~1500/52%)→src/proto.cpp;flat.hpp 壳辅助 ROI 低建议保留;flat_schema.hpp 100% 已迁移。样板 = flat 家族(私有前向声明/非模板去 inline/数据类留头/头 include 自给)。官方 wire 对拍全部一致,无 wire 不兼容。
- [已文档化非新问题] RTTI_ENABLE_FLAT 与 RTTI_ENABLE_FLATBUFFERS 错位(README caveat,双审 ACCEPT)。已核实无问题:varint 10 字节/zigzag/packed/深度/字段 0/扩展注册冲突/flat 布局对齐/vtable soffset/UTF-8/file_identifier/ODR。
- 修复完成后:双审复核(复用审计A/B 会话)→ 剩余迁移批次(xml/json/ini/toml 大户 + to_string text_node + convert 桥)→ 统一收尾复跑基线 → 一次性报告 → update_goal complete 评估。

### 2026-10-08 批量救护/中断恢复(途中快照)
- 5 个修复/迁移子代理多次被救护(工具超时)。已按 discover 教训恢复:每会话恢复 prompt 强制「先 relay 心跳→每一步 relay 进度→构建拆短命令」。已收到全部 5 个心跳:
  - F-A(ses_ee841045dffei3pelW8yLeJrFt):已确认基线 55642/0,已定位 4 处(A-1 memory.cpp:87-111/A-2 convert.hpp:1090-1106/A-3 reflect.hpp:770/A-4 ioc.hpp:276-282),开始 TDD。
  - F-B(ses_ee840ccfdffeom3i7HaGRKiYIx):busy,恢复续做 B-1/B-2,定位工具链+基线构建阶段。
  - F-C(ses_ee8409036ffeSqe4NIH4NMMFrV):busy,基线已确认 0e0w+55642/0,开始 TDD C-1..C-4。
  - F-D(ses_ee83141e5ffeKi0aGXamhm1BhU):已确认 4 处位置(D-1 flat_schema.cpp:1590/D-2 proto_schema.hpp:1770/D-3 :1895-1910/D-4 flat.cpp:553-561),定位工具链后 TDD。
  - M2(ses_ee840468cffekgG49XUkBgCF1u):date_time.hpp 已声明化约 169 行(构造/工厂/访问器/format 三态),parse+私有辅助待剪;src/date_time.cpp+src/scan.cpp 未建(危险半成品,须完成迁移恢复构建)。
- 之后目标被用户中止(paused,user aborted)。子代理后台仍可能 busy 推进。恢复时:worktree 取证→收各队回执→逐队核验 commit→双审→剩余迁移→统一基线→一次性报告。

### 2026-10-08 恢复后取证(用户 /goal resume 恢复 active)
- **F-B 完成 commit 3a4d409**(fix(ini/toml) ERANGE+too_deep,+45/-3,4 文件:ini.hpp as_integer/as_real 补 errno==ERANGE;toml.hpp inline-table read_path 失败保留 too_deep)。主代理 diff 核验符合审计(ini.hpp:188-195/207-214 补判;toml.hpp:1503-1509 `if (err.code != too_deep)`)。测试 ini_test/toml_test 分别 +22/+18 行红测。
- **F-C 完成 commit 8ab32fb**(fix(xml/json) C-1..C-4,+109/-15,5 文件:json new_node 判空×N+memory.hpp 注释改正+xml 数字实体 cp>0x10FFFF 上界+数字最少 1 位+xml document zero-copy 契约注释(源缓冲须存活)+xml serialize buffer 覆盖语义)。主代理 diff 核验符合审计。
- 在途:M2(date_time.hpp 半成品 -154/+15 仍 M,src 未建,须完成恢复构建)/F-A/F-D 均 idle 无产物,已再次 prompt 续做(强制一次闭环+每步心跳+拆短构建命令)。
- F-B/F-C 未在共享树跑全量(并行 M2 半成品会断链接);统一收尾时主代理复跑全部套件验证断言基线。
- **F-A 完成 commit 9dedef8**(fix(core) A-1..A-4,+81/-7,7 文件:memory.cpp 跨块 align pad 重算/convert.hpp fixed_sequence_from 长度校验/reflect.hpp set<real> key 用 format_real 最短/ioc.hpp 句柄生命周期注释)。红测 memory_test/static_array_test/reflection_iter_test。红=55642/4 新断言失败,绿=55667/0。
- **主代理独立复验(2026-10-08)**:全量构建 0e0w;rtti_tests **55667/0**、proto_ext 160/0、flat_schema **403/0**(F-D D-1 union id==total 测试已 +2)、build-off **32790/0**。M2 迁移产物 src/date_time.cpp+src/scan.cpp 已建且 CMake 接线,rtti 库编译通过。
- 在途:M2(date_time.cpp/scan.cpp 未跟踪待 commit)/F-D(flat_schema.cpp 已在工作树,D-2..D-4 待续)。已催 commit+续做。
- **M2 迁移完成 commit 72096c7**(+582/-487,5 文件:CMakeLists +2 / date_time.hpp -381 声明化 / scan.hpp -141 / src/date_time.cpp +398 / src/scan.cpp +147)。聚焦对照 g[1eb270a] 基线库 517/0 vs 新库 517/0 断言一致(迁移自身零新增断言,行为等价铁证)。主代理 git show --stat 核验一致。
- 主代理先前复验(含 F-A/F-B/F-C 修复):全量 0e0w,rtti_tests 55667/0、proto_ext 160/0、flat_schema 403/0、build-off 32790/0。待 F-D 完成后统一终验。
- 仅剩 F-D(D-3 作用域/D-4 u64 未完成);其后双审+剩余迁移批次+一次性报告。

### 2026-10-08 全修复闭环 + 主代理终验
- **F-D 完成**:D-1 union id>=total(0xC0000409 崩溃→ASan 403/0)、D-2 enum INT64_MAX 饱和守卫、D-3 作用域感知类型解析(protoc 语义)、D-4 u64≥2^63 钳制文档化+flat_test 边界、顺手 Minor(实数指数 1.5e3 解析/union offset 注释)。主代理独立核对 diff 全部符合审计;构建 0e0w;rtti_tests **55696/0**、flat_schema **408/0**、proto_ext 160/0、build-off **32790/0**。主代理收尾 commit **ed2e813**(7 文件 +280/-15)。
- **6 commit 全链**:1eb270a(M1 base_convert+naming)→3a4d409(F-B ini/toml)→8ab32fb(F-C xml/json)→9dedef8(F-A core)→72096c7(M2 date_time+scan)→ed2e813(F-D proto/flat)。HEAD=ed2e813,工作树仅 .opencode/doc 文件。
- **非模板迁移进度**:src/ 现有 13 cpp(base_convert/naming/date_time/scan/common/reflect/error/memory/string_ops/number_format/format_reflected/flat/flat_schema)。base_convert+naming+date_time+scan 已迁;待迁大户:json(91%)/xml(97%)/ini(94%)/toml(96%)文档+parser、to_string text_node(48%)、proto_schema parser(80%)。用户硬性要求「大量本该在源文件的东西必须迁入」。
- 下一步:双审复核 6 commit(复用审计 A/B/C scoped re-review)→ 迁移后续批次 → 统一终验 → 一次性报告 → complete 评估。

### 2026-10-08 三审计复核全部 Accepted(6 commit 双审闭环)
- **审计A**(1eb270a M1/72096c7 M2/9dedef8 F-A):全部 Accepted。要点:默认实参仅留头声明(如 to_base digits=nullptr)去重正确;SSE2 门+双路径完整搬入 src/scan.cpp;F-A 四项与审计原定位一一对应。无 rejected。
- **审计C**(ed2e813 D-1..D-4):Accepted。union 越界向量 OOB 消除、enum 溢出饱和守卫与 flat_schema.cpp:1212-1214 逐字一致、D-3 作用域解析回归保持性逐条推演(forward/map-entry/跨作用域/绝对引用不回归)、D-4 文档化+FlatU64BoundaryRoundTrip 全路径测试、顺手 Minor(1.5e3/union offset)修法正确。遗留建议(不阻塞):extension/en天ee 解析仍未作用域化、enum 超 int32 静默截断(原 PS2 Minor)、flat u64 typed 编码依赖 convert overflow。
- **审计B**(3a4d409 F-B/8ab32fb F-C):Accepted,附 1 处必修正 + 1 同型遗留:
  - [必修正] xml.hpp:529-533 C-2 注释错引:"exactly like json.hpp's document" 与 json.hpp:20-23(全复制自持,视图源缓冲销毁后仍有效)矛盾——XML 才是四格式唯一零拷贝异类。修复指令:改写为对比表述"unlike the json/ini/toml adapters, which copy every stored string into the document's own arena, the XML document borrows ... into the parse source buffer"。
  - [同型遗留·C9] xml.hpp:466-469 make_view 返 nullptr 后 v->target 解引用、ensure_root(create_node null)仍未判空——与 C-4 json OOM 判空同型;xml 侧 OOM 未在本轮覆盖。
  - [不阻塞] ini/toml pool_alloc(new→bad_alloc)vs json/xml arena(null)分配策略差异未统一(toml.hpp:26 "never throws" 措辞违规)。
- 双审复核完成、三审计盖章。
- **F-C2 续做完成 commit ab9d190**(fix(xml) R1-C2+C9,+43/-9,2 文件:xml.hpp 注释对比表述修正——unlike json/ini/toml 全复制自持、XML 唯一零拷贝;make_view×3/ensure_root/write 侧 root_ 判空 C9 同型 OOM 补守;paser create_node null→overflow)。主代理核验:全量 55696/0、xml+json standalone 477/0、0e0w。仅 2 文件 commit 未触碰并行组改动。
- 进入迁移分派(M3 json/M4 toml/M5 ini→src 后台执行中):
  - **M5 ini 完成 commit 6ca7cbf**(refactor: move ini non-template impl to src,3 文件 +1143/-988:CMakeLists +4/ini.hpp 1041 行声明化/src/ini.cpp 1086 行新增)。迁移:node 成员虚函数/pool_alloc/文本工具/document 虚函数+open_section+parse/parse_stream/serialize 全移入;头保留公开声明+类壳+write_config+parse_events+模板壳+kMaxSectionDepth constexpr。主代理收到回执,独立验证 199/0 与迁移前基线一致、ODR 干净、rtti target 入库成功。共享全量构建暂被并行 toml/json 在途阻挡,收尾统一复跑。
  - **M3 json 完成 commit ac6b814**(refactor: move json non-template impl to src,2 文件 +1258/-1223:json.hpp 1470→232 行纯声明化/src/json.cpp 1246 行全定义)。迁移要点:公开声明+模板壳+默认实参留头去重,vtable 单 TU 发射。独立验证 239/0 与基线一致,库集成成功。JSON.cpp 行已随 6ca7cbf 入库(json.cpp 行由 ini 组先行加)。**M5/M3 均已 commit**(ini 6ca7cbf、json ac6b814)。
  - [在途] M4 toml(toml.hpp 1933→323 声明化完成,toml.cpp 1742 行已写,独立验证中)。

### 2026-10-08 四格式迁移全部闭环 + CMake 门控修复
- **M4 toml 完成 commit 17dd340**(refactor: move toml non-template impl to src,3 文件:CMakeLists 仅加 toml.cpp 行/toml.hpp 52+/1662- 声明化 1933→323/src/toml.cpp 1758 行包含 79 处定义)。parser::fire 成员模板保持类内 inline 随 parser 入 cpp(MSVC 类外模板成员限制);默认实参 cfg={} 留头去重;vtable 由 ~document() 发往 cpp。独立 271/0 与基线一致 + 完整套件 55696/0 复验。
- **四格式迁移全落地**:M2 date_time+scan(72096c7,M1 base_convert+naming 1eb270a)+M5 ini(6ca7cbf)+M3 json(ac6b814)+M4 toml(17dd340)。src/ 现 18 cpp。
- **主代理收口发现并修复 build-off 门控问题 commit ce67c6a**:7dd340 后 add_library(rtti) 无条件含 src/ini.cpp/json.cpp/toml.cpp,build-off(全门关)编译这些 cpp include 门控头触发 #error C1189。修复:ini/json/toml 源改 `if(RTTI_ENABLE_*) target_sources(rtti PRIVATE ...)` 门控追加(与 tests/CMakeLists 门控一致)。复验:主 build 55696/0、build-off 32790/0、proto_ext 160/0、flat_schema 408/0、双构建 0e0w。HEAD=ce67c6a。
- 剩余迁移批次(评估文档+审计清单):M6 xml(~97% 大户,注意 F-C2 已 2 轮动 xml,OOM 修正后 XML 仍有零拷贝契约,迁移不得改自持,header external 在 ab9d190 已是注释对比表述)、M7 to_string text_node+parser(~48%)、M8 proto_schema parser(~80%)。proto.hpp 非模板 wire(~52%)次优。

### 2026-10-08 M6/M7/M8 迁移(三组并行,后台)
- **M7 to_string text_node 完成 commit 12c03d13**(refactor: move to_string text_node impl to src,3 文件 +818/-742:to_string.hpp 1158→452 行保留 detail::text_node 类声明+模板/text_parser 整结构 8 成员迁出/src/text_node.cpp 781 行含 text_node 成员+析构(=default 发射 vtable)+text_parser+parse_text)。CMake 源列表后加 src/text_node.cpp(无门控)。独立验证 string_convert_test 8680/0、format_test 130/0 与基线一致,0 警告,库集成成功。git add 仅 3 文件。
- [在途] M6 xml(M2 组后 xml.hpp 2021 行,基线 238/0,续做正式开始落盘,分段 edit+Write)、M8 proto_schema(基线 450/0+proto_ext 160/0,src 110KB 已写株 D-3 原样,头声明化/compile_schema 定义/CMake gate 接线中)。

### 2026-10-08 M8 完成
- **M8 proto_schema 完成 commit 2bb796a4**(refactor: move proto_schema parser impl to src,3 文件 +2936/-2828:proto_schema.hpp 2857→268 行=公开声明+数据成员+compiled_schema 类壳+detail 仅 fwd;src/proto_schema.cpp 2904 行=scanner/parser 各 parse_*/resolve_types/compile 桥/dynamic_field codec)。验证:独立编译 0 警告、proto_schema_test 450/0 持平、官方 fixtures proto_ext 160/0 持平、proto_test 800/0、库集成成功。**D-3 作用域解析(ed2e813 权威态)与原 77 行 BYTE-IDENTICAL**,未简化。git add 仅 3 文件。
- [仅剩] M6 xml(在途,落盘中)。

### 2026-10-08 M6 完成 + 全目标收尾终验
- **M6 xml 完成 commit 72aaffb**(refactor: move xml non-template impl to src,3 文件 +1920/-1786:xml.hpp 2021→281 行=公开声明+配置+模板壳;src/xml.cpp +1871 行=node 全类(零拷贝 string_view 语义原样)/document 全成员 vtable 在 cpp 发射/xml_parser 全 struct/masks+find/parse+parse_stream+serialize(默认实参留头)/序列化器)。验证:独立编译 0 警告、xml_test 238/0 基线链路一致①-④全过、库集成无 ODR。CMake 加 RTTI_ENABLE_XML 门控块。
- **8 迁移 commit 全落地**:M1 1eb270a(base_convert+naming)→M2 72096c7(date_time+scan)→M5 6ca7cbf(ini)→M3 ac6b814(json)→M4 17dd340(toml)→M7 12c03d13(text_node)→M8 2bb796a4(proto_schema)→M6 72aaffb(xml),+CMake 门控修复 ce67c6a。HEAD=72aaffb,工作树仅 doc 文件。
- **src/ 现 21 cpp**:base_convert/common/date_time/error/flat/flat_schema/format_reflected/ini/json/memory/naming/number_format/proto_schema/reflect/scan/string_ops/text_node/toml/xml,计 19 个实质性实现文件(+common/error 基础)。
- **统一基线终验(主代理)**:主 build 0e0w,rtti_tests **55696/0**、rtti_proto_extension_tests 160/0、rtti_flat_schema_tests 408/0;剥离 build-off **32790/0** 0e0w。迁移+修复后断言数与迁移全落地前基准完全一致=行为零变化铁证(迁移零新增断言,修复期断言数 55642→55667→55696 由 F-A/F-D 新增测试)。

### 全目标 W6 验收(最终)
- [完成] 多团队并行审计:审计A(核心链)/审计B(格式)/审计C(proto-flat)三团只读审计,产出 Critical/Important/Minor 全带 file:line 证据。证据:process.md 审计聚合段。[READ:process.md]
- [完成] 真问题修复:2 Critical(D-1 union OOB/D-2 enum 溢出)+8 Important(B-1 ini ERANGE/C-1 xml 实体回绕/C-3 buffer 语义/C-4 json OOM/A-1 arena 对齐/A-2 fixed_sequence 截断/D-3 作用域解析/D-4 u64 钳制)+6 Minor 全修复落地(F-B 3a4d409/F-C 8ab32fb/F-A 9dedef8/F-D ed2e813/F-C2 ab9d190);双审复审计(A/B/C)全 Accepted 盖章。证据:git log 6c follow + 审计复核报告。[BASH:git log]
- [完成] 非模板实现迁入 src:8 迁移 commit,M1-M8 覆盖 base_convert/naming/date_time/scan/ini/json/toml/text_node/proto_schema/xml 全部提名大户(审计+评估交义确认),头 281~450 行仅声明+模板壳,默认实参留头去重,vtable 单 TU 发射。证据:git show --stat。[BASH:git log]
- [完成] 行为零变化:统一终验主 55696/0+剥离 32790/0+proto 160/0+flat 408/0 双构建 0e0w;迁移零新增断言(各迁移独立基线对比一致)。[BASH:rtti_tests]
- [完成] 编译体积:include/rtti/ 22 头非模板实现主体迁出,src 21 cpp,用户硬性要求(编译体积膨胀红线)达成。证据:src 文件清查。[BASH:Get-ChildItem src]
- 测试范围原则(r24,用户明确):只测本次改动相关项及可能影响项,C10;全量门仅最终收尾统一跑一次;Debug=权威判据(Release exe 属陈旧产物)。

## 已完成阶段归档(裁剪版,证据锚点保留)

### 反射核心 + 三格式适配器(2026-10-05→06,166/0 起步)
- Task1-12 骨架→error/arena→字段表→宏族→convert→运行时API→剥离→双语→基准 全完成,双构建 166/0(build 与 build-off)。
- 扩展:queue/stack/pq 适配、运行时容器操作、type_desc 惰性解析;329/0。
- 工程:JSON(200/0)/XML(188/0)/INI(160/0),主 877/0+build-off 329/0。集成:枚举转换统一入 convert.hpp、测试枚举跨 TU 唯一命名、tests/CMakeLists 门控条件追加。
- 收尾:流式 SAX + serialize(buffer) 预分配 + 剥离矩阵八组合,主 967/0。
- 关键 commit/基线见 docs/superpowers/plans/2026-10-05-rtti-reflect-core.md。

### 性能优化(SIMD scan + arena 节点池,2026-10-06)
- scan.hpp(SSE2+标量双路径 278/0);JSON +46%/XML +24% 吞吐(checksum 一致);主 1245/0、core 607/0、剥离八组合全绿。痛→discover(PowerShell $($c.j) 传参陷阱)。

### 性能与易用性强化阶段(2026-10-06,用户 modern C++=C++17)
- spec: docs/superpowers/specs/2026-10-06-perf-usability-design.md(A allocator/less、B 新类型、C 性能、D 配置)。
- SDD 波次 A-P 全部核验(主 32266/0):A custom allocator/less、B 新类型、C 浮点统一+键哈希+SIMD、D naming、E 迭代器、F TOML、G 数组、H protobuf、I date_time、J base_convert、K to_string、P 宽容转换、L Attribute、M 实例化、IOC、N type_judge C4002。
- §20 抽象/虚函数+模板反射(32163/0)、§22 方法反射(32266/0)。
- examples 7 demo 全过;剥离矩阵 32 组合全绿(30274→32266 单调);build-off 30274/0。原有目标 update_goal complete(2026-10-06)。

### 深度验证阶段(2026-10-07,六维审计→R1-R5 修复)
- 六维审计:①API/文档 C1 key_case snake/camel+6 Minor;②核心安全 3 Critical 栈溢(ini/toml/convert)+I1-I8;③可移植 C1(编译器下限 GCC11+/Clang14+)+I1 LP64+I2 宏空参;④测试有效 无 Critical;⑤spec 符合 §13.2 .proto 解析器未实现;⑥遗留 K-1 uint64 截断/K-2 date_time 亚秒/虚继承 dynamic_cast/共享流 .0。
- 用户裁决 §13.2:"两个都需要"(schema 注册 + .proto 文本解析器)。
- R1 序列化安全 / R2 生命周期 / R3 宏+测试 / R4 文档 / R5 .proto 解析器 全完成核验:主 32266→54738/0、剥离 30274→32462/0。
- 双审复审计(A1 核心/A2 测试文档):R1/R2/R3/R4/R5 通过;残留(R5 sint zigzag/boolean 树表示、R2-2 多句柄 destroy、R3-1 括号实参)已并入新功能计划处理。

### 新功能阶段:proto V2-V3 全版本 + FlatBuffers(2026-10-07,11+1 任务全交付+双审 ACCEPT)
- spec: docs/superpowers/specs/2026-10-07-proto-v2v3-flatbuffers-design.md;官方工具 protoc 36.2 + flatc 25.12.19(仅 fixtures 对拍)。
- commit 链(线性单父,基线 73b6738):Task1 8893bab(fixtures 先行)→ Task2 b4e3ea1+47e441d → Task5 6612f5c+ecd0209 → Task6 e23d7b7+3a77063 → Task7 1352074+2c30d79 → Task8 645e657+22ef9c4 → Task8a 5851ad8 + T-A 2af40ac(9 src 接线)→ FIX 05ddb1c → P0 cd4d5a3 → Task3 ddf1464d → Task4 5588ba9 → Task4 修复 2c807f7 → Task10 832a226b → Task11 文档 556c0e5(HEAD)。
- 基线(Debug):主 rtti_tests **55642/0**、rtti_proto_extension_tests 160/0、rtti_flat_schema_tests 401/0、build-off(六门全关)**32778/0**、64 组合(6 门)全绿单调、9 demo cl 0 警告。
- 双审一致 ACCEPT(零 Critical):Imp-1 FLAT 门控语义已文档化(README.en:486-495,CMake RTTI_ENABLE_FLAT 仅控测试注册,src/flat.cpp 恒编入,完全剥离需额外 -DRTTI_ENABLE_FLATBUFFERS=0);Mn-1 预存 rtti_test_lib::Inner 跨 TU ODR(基线即有,记录不修)。
- 遗留(非阻塞,记录在案):Inner ODR、\uD800-\uDFFF 代理对未校验(M-3 剩余)、包限定 extendee 名不支持(头注释建议注明)。

## 2026-10-08 新目标:迁移后深度研究优化(四项全选+六缺口全修;spec 0152550 plan post-migration-deep-verify)
- 背景:上一目标(审计+8批迁移)complete,HEAD=72aaffb 后追加 spec/plan commit 0152550。src 19 cpp + include 22 头。
- **Task1 三审计团二次审计(只读)全部收齐**:
  - 审计A(核心链,ses_ee5455027):ODR 单一定义/签名/默认实参/vtable 单 TU 全部通过;9 cpp /W4 独立编译零警告;白盒探针 ~40 组链 rtti.lib 全绿。注释 6 过期(重点 error.hpp:12 "later, format adapters"、convert.hpp:3-7 "future adapters"+Task7 引用残留、string_ops.hpp:120 surrogate 句错位、reflect.hpp:100 语法破损、ioc.hpp:3 "task M")。文档 4 组不同步(README 断言数 55696/32790/408 过期、README.en:301 "header-only" 过时、migration 快照)。G-4 核心链已实现(text_node.cpp:696-731、string_ops.cpp:410-430)。
  - 审计B(格式,ses_ee54529b):6 TU 独立编译+全 19 TU 全新链接 /W4 0w0e;harness 30/30。G-4 四格式均实现。G-6 代码已实现但 toml.hpp:26-29 "never throws" 与 pool_alloc(new 可抛)冲突。待办均文档:①toml.hpp:26-29 措辞②README 双语"纯头文件"过期+断言数过时(32778→32790/55642→55696/401→408)③nontemplate-migration.md 快照④json.cpp:111 缩进、ini.cpp:286 闭括号⑤rtti_fwd.hpp:28 六宏注释五宏。
  - 审计C(proto/flat,ses_ee544dbb):8 文件 ODR/签名/vtable/默认实参核对过;三 src 编译零警告;探针链 rtti.lib 断言 0 失败。**G-1【缺失】extendee 类型解析全局无作用域(proto_schema.cpp:1955/2709 空 scope)、G-2【缺失】enum 超 int32 截断(:1627-1628,2147483648→-2147483648)、G-5【缺失】包限定 extendee 名不支持(package 被吞,extend foo.Baz 报 code=20)**。G-3【已实现+文档完备】flat u64 typed overflow 显式+decode 钳制+测试锁定。注释 5(2 与 G-1/G-5 佐证)、文档 4。
- **裁决(logger)**:①G-1/G-2/G-5 同源 src/proto_schema.cpp,合并单修复组(避免同文件并行冲突)。②Task2 注释补强范围排除 proto_schema.cpp 功能行(其注释并入缺口组);toml/ini 注释(G-6)归入注释组。③G-3 已实现→Task6 降级为锁定/报告,不入功能修复。④G-4 已实现→Task7 降级为补测试锁定+报告。⑤Task2/3 与 G 组按文件隔离并行。
- 下一阶段:派发 Task2(注释组)+Task3(文档组)+Task4/5/8(缺口组同期)。
- win32 PS5.1;MSVC2022 Enterprise;cmake="C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe";生成器 "Visual Studio 17 2022" -A x64;C++17;/W4 /permissive- /Zc:preprocessor 零警告硬门;build-off 剥离构建复用同路径。
- 索引:memory.md=跨项目经验;discover.md=失败档案;plan.md=方案与待办。

## 2026-10-08/09 新目标:最高优化档零告警 + cast/as 双向变体(spec fd7d973,plan dbfb169)
- 背景:上一目标 complete 后用户提出两项:(1)build-opt(/O2 /Ob2 /GL + /LTCG)非零告警须修复;(2)反射支持 C# 风格 cast/as 双向变体,三种操作数(原始指针/object_ref/instance)。用户已澄清:三操作数全支持、沿继承层次双向、类型擦除全精度、子代理默认模型。
- 执行模式:Subagent-driven(SDD),主工作树;.superpowers/sdd/2026-10-08-cast-as-variance/ 为 ledger + 报告。
- **Task1 完成 commit caf821c**(零告警:test_harness summary()→返回退出码;test_main return;CMake RTTI_ENABLE_LTO + check_ipo_supported;补 rtti_bench)。审阅 approved。基线 55774/0。
- **Task2 完成 commit e69c725**(base_adjuster/adjust_to 基链 thunk;虚基上转 static_cast 偏移正确为承重 Ruling)。审阅 approved(+1 fix)。基线 55778/0。
- **Task3 完成 commit 37031b3**(rtti::as/cast 自由函数 + object_ref 成员;上转 static_cast/base_adjuster、下转/无关 dynamic_cast 需 is_polymorphic 守卫)。首轮审阅 Changes Requested(I-1 const 硬错误/I-2 虚基注释错/I-3 多态覆盖缺 + M-1/M-2)。
- **Task3-fix 完成 commit f7a8624**(const 源/目标 static_assert 拒绝;虚基注释纠正;object_ref const 重载;JudgeTestAsCastPolymorphic 多态下转/跨转/非零偏移覆盖)。复评全 CLOSED PASS。基线 55798/0。
- 计划修订 commit d8da9d2(虚基上转合法)+ d0f9b0b(Task3-fix 段 + README 文案)。README 文案错误(虚基机制)已先行修正,Task5 勿再写错。
- 承重经验:①子代理 idle≠完成,须以 commit/绿证/报告三产物核验;②「实现与计划逐字一致」不等于正确,缺陷可在计划层,须先改计划再派修复;③模型/Provider 层错误(choices:null)须与任务失败区分,禁擅自换模型;④task 工具无 model 参数,新子会话只能继承会话默认模型。
- 进行中:Task4(instance 全精度 as/cast,类型擦除基链 thunk,BASE=f7a8624,plan:587-838);待 Task5(文档双语+边界补测+三档四套件终验)。

## 2026-10-09 工业化阶段(用户新增,库即将闭环)
- 用户 /goal resume 追加范围:(1)反射完整化——枚举遍历(全部值/名/元数据)、字段遍历 for_each_field、模板类型反射增强 + 「全新操作」;(2)完整 GitHub 自动化 CI/CD;(3)超级详细严谨、严重尊重事实的 benchmark;(4)测试/构建/文档/版本管理等工业化基础设施。要求 GCC 零警告、Linux/Windows 均可跑通、中英双语文档互相索引。
- 现有基础设施探勘(事实):无 `.github/`;无 LICENSE;无版本文件(仅 CMake `project(RTTI VERSION 0.1.0)`);`scripts/` 仅 `gen_fixtures.ps1`;`bench/` 有 bench_parse.cpp/bench_reflect.cpp/bench_timer.hpp(基础);CMake 无 install/export/CPack;`.gitignore` 存在。枚举反射:`enum_table`/`enum_to_string`/`enum_from_string`(reflection.hpp:1248-1277),enum_desc 仅 names。模板反射:`template_info`/`is_template_instance`/`template_arg_count`/`template_args`/`template_args_of`/`template_arg_desc_of`(reflection.hpp:1656-1730)。字段:有 `fields<T>()`/`field_count<T>()`/`field_begin/end`,无 for_each_field。
- **本机工具链事实:仅 MSVC cl(14.44.35207),无 g++/clang++** → GCC/Linux 验证只能靠 GitHub Actions ubuntu runner,本地无法代验。tests 42 个 *_test.cpp;主基线 55819/0。
- 计划分解:A Task5 收尾 / B 反射完整化 / C GitHub CI/CD / D benchmark / E 工业化基础设施(版本/License/打包/双语互引)。按 brainstorming architectural 路径:先探勘→设计简报→(待用户批准)→spec→plan→实施。
- 「崩溃」澄清:非真崩溃,系①目标无进展自动暂停(两轮只读)→②工具超时(大构建/大文件)→③Provider 瞬态错误(choices:null)。
- [2026-10-09 工业化推进] B 反射遍历(daee44ed/a77cf12/affb574)双审盖章 APPROVED,独立 55846/0 0e0w。Task5 审阅 Changes Requested(README:69 空悬 Layout)→ 修复 ee417ed(删2行)→ scoped 复核在途。N1 裁决:B 正式审阅判计划文档内部矛盾非缺陷,enum_count 未注册=编译期硬错误系 fail-fast 既有约定(fields_of 同),留 E4 文档注明。历史上百 idle 子代理已批量 abort。

- [2026-10-09 工业化闭环收尾] 全部任务双审盖章:E1 6212a40/E3 973d5d4/E2(并入6212a40,竞态记录)/C f576b40/D b66405b/E4 9bdcf97+fix 109c41a。HEAD=109c41a。终验:Debug rtti_tests 55847/0、build-off 32872/0、proto_ext 160/0、flat_schema 408/0;Release 55847/0;LTCG 55847/0 0w0e。Variance casts b553df8+ee417ed 全链 Approve。

## 2026-10-09 文档体系补齐闭环目标(active→complete)
- 目标:docs/interface、docs/design、docs/benchmarks 三目录中英文档齐全且全部事实可核、三 README 索引连通(目标文本见 get_goal)。
- **commit 链(本目标,HEAD=8268403)**:
  - c1753eb docs: reflection macro family reference (EN/ZH, ch 18-27)——EN+ZH 同步追加宏族(§18 宏基础设施 RTTI_CAT/RTTI_EXPAND/RTTI_IS_EMPTY/RTTI_NARG/RTTI_APPLY_*、§19 RTTI_FIELD_* 字段注册辅助宏、§20 RTTI_DEFINE_TYPE* 族、§21 RTTI_DEFINE_ENUM、§22 模板信息注册宏、§23 属性宏、§24 构造器宏、§25 方法注册宏、§26 头结构行号布局表 + 尾 include convert.hpp 打破环、§27 其它语言互引)。布局表行号与 reflection.hpp 实笔一致(grep 1203/1213/1259/3520/3708/3737/3892/3922/4040/4075/4105/4145)。
  - 8756fa2 docs: reflect tools interface reference (EN/ZH) + index wiring——新建 docs/interface/{en,zh}/reflect-tools.md 覆盖 reflect.hpp(§1-3 运行时查询/迭代器/反射实例化+ORM)/ioc.hpp(§4 DI 容器)/to_string.hpp(§5 统一字符串层)/string_ops.hpp(§6 rtti::str);docs/README.md 拆 utils 与 reflect-tools 两行;README{,.en,.zh-CN}.md 索引同步拆条。
  - 6ddc999 docs: performance report LTCG tables + assertion baseline cross-check (EN/ZH)——EN+ZH 报告新增 §3.3/§4.1 LTCG 表(build-opt 实跑,落盘 docs/benchmarks/2026-10-09-LTCG-*.txt)、§5 断言基线对照表(Debug/Release/LTCG/build-off 全实跑:rtti_tests 55847/55847/55847/32872、proto_ext 160、flat_schema 408),旧 §5/§6 顺延为 §6/§7。
  - b744b4b docs: fix attr_arg factory names + broken cross-links (fact-check pass)——修正 zh/reflection.md:616 编造 API `attr_arg::make_int`/`make_str`(真工厂 attr_arg::integer/real/text/boolean,详情见 discover.md);修 4 处断链(README.en/zh 的 `../docs/README.md`→`docs/README.md`;reflect-tools en/zh 的 `../design/…`→`../../design/…`)。
  - 8268403 docs: superpowers spec/plan history + gitignore——
    .gitignore 增 build-opt/、.opencode/backups/;纳入 docs/superpowers/plans/2026-10-07-proto-v2v3-flatbuffers.md、specs/2026-10-07-…design.md、specs/2026-10-06-perf-usability-design.md(历史 spec/plan 追记,与已跟踪 2026-10-08/09 spec 一致)。
- **事实性核查(全面 pass)**:链接脚本扫描 29 个 md 全部相对链接 OK;API 名存在性自动检查(`` `name(` `` 提取比对 include/rtti/)仅命中 utils.md CMake 例(非 API)与已修 zh/reflection.md make_str;RTTI_* 宏名全量比对零缺失;bench 数字与 4 个 txt 逐字一致(enum_count 1.02/field_walk 954016409.1/…);LTCG 数字与落盘 txt 逐字一致。
- **本目标 W6 验收见 plan.md「2026-10-09 文档体系补齐闭环」节**,全部 [完成]。
