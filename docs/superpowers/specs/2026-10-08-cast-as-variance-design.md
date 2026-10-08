# cast/as 双向变体转换 + 最高优化档零告警设计

- 日期:2026-10-08
- 状态:定稿(用户批准;子代理用默认模型)
- 基线:HEAD=92291da;主 rtti_tests 55774/0、剥离 build-off 32800/0、proto_ext 160/0、flat_schema 408/0(Debug 权威)。Release(/O2)同绿。

## 背景与目标

用户提出两项要求:

1. **Release 不代表正确**:要求验证优化构建(`/O2`,以及更高优化形态)能干净编译。实测:普通 Release(`/O2`)全绿;新建 `build-opt`(`/O2 /Ob2 /GL` + `/LTCG`,MSVC 无 `/O3`,此为最强优化形态)全量构建**非零告警**——必须修复。
2. **反射支持逆变协变**:新增 C# 风格 `cast`/`as` 语法糖——`as` 转换失败返回空指针;`cast` 为强制转换,失败返回错误码。作用对象三种操作数(原始指针、`object_ref<T>`、类型擦除 `instance`)全支持;沿继承层次**双向**转换。

## 工作流 A:最高优化档零告警

### 问题(证据)

`build-opt` 全量构建(`cmake -S . -B build-opt -G "Visual Studio 17 2022" -A x64 -DCMAKE_CXX_FLAGS_RELEASE="/O2 /Ob2 /GL" -DCMAKE_EXE_LINKER_FLAGS_RELEASE="/LTCG"`)输出:

- `C4702` 无法访问的代码 @ `tests/test_main.cpp(12)`:`rtti_test::summary()` 内无条件 `std::exit(...)`,普通构建因未跨 TU 内联未报;`/GL` 全程序优化后编译器证明其后 `return 0;` 为死代码。
- `LNK4075` 忽略 `/INCREMENTAL`(因 `/LTCG`):链接器提示,`/LTCG` 与增量链接冲突。

### 修复方案

- **C4702**:`rtti_test::summary()` 改为返回退出码(`int`),不再自行 `std::exit`;调用点改为 `return rtti_test::summary();`。涉及 `tests/test_harness.hpp`(`summary()` 定义 + `RTTI_TEST_MAIN()` 宏)与 `tests/test_main.cpp`。消除不可达代码,语义更清晰(退出码由 `main` 返回)。
- **LNK4075**:在根 `CMakeLists.txt` 提供选项 `RTTI_ENABLE_LTO`(默认 ON,仅 Release 生效)或设置 `CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=ON`;由 CMake 联动关闭增量链接,消除 LNK4075。
- **验收**:`build`(Debug + Release)与 `build-opt`(LTCG)四套件全绿 + 全量 **0 warning 0 error**(C0 硬门覆盖最高优化档)。

## 工作流 B:cast/as 双向变体

### API 面(三种操作数全支持)

```cpp
namespace rtti {

// 原始指针(自由函数):T 为目标, U 为源静态类型
template <typename T, typename U>
T* as(U* p) noexcept;              // 失败 -> nullptr
template <typename T, typename U>
result<T*> cast(U* p);             // 失败 -> error_code

// 引用重载(可选便利)
template <typename T, typename U>
T* as(U& r) noexcept;
template <typename T, typename U>
result<T*> cast(U& r);

} // namespace rtti

// object_ref<U> 成员
template <typename U> class object_ref {
    template <typename T> T*         as() noexcept;
    template <typename T> result<T*> cast() noexcept;
};

// instance(类型擦除)成员
class instance {
    template <typename T> T*         as() noexcept;
    template <typename T> result<T*> cast() noexcept;
};
```

### 语义(仿 C#)

| 情形 | `as<T>` | `cast<T>` |
|------|---------|-----------|
| 源为空(空指针/空句柄) | `nullptr` | `result` 错误码 `no_such_field`(与现 `dynamic_cast_ref` 一致) |
| 转换失败(无关类型) | `nullptr` | `result` 错误码 `kind_mismatch` |
| 转换成功 | `T*` | `result<T*>`(`ok()==true`,`.value` 有效) |

- `as`/`cast` 均为**运行时安全**:失败不崩溃(spec §0)。
- 成功返回的指针**借用**源对象(生命周期 = 源句柄/对象生命周期),不转移所有权。

### 变体规则(沿继承层次双向)

- **协变 / 上转(Derived→Base)**:源静态类型 `U` 派生自目标 `T`(`is_base_of_v<T,U>`)时,编译期安全。
  - 单一/多继承、虚基类:指针调整由编译器在 `static_cast` 中完成(虚基类偏移正确)。
- **逆变 / 下转(Base→Derived)**:源静态类型 `U` 是目标 `T` 的基类时,需运行时检查。
  - 若 `U` 为多态(`is_polymorphic_v<U>`):用 C++ `dynamic_cast<T*>(p)`(失败返回 `nullptr`)——编译器保证正确偏移,含虚基类/多继承。
  - 若 `U` 非多态:无法运行时判别,`as` 返回 `nullptr`、`cast` 返回 `kind_mismatch`(文档化:非多态下转不支持)。
- **同类型**:直接返回(协变特例)。
- **无关类型**:编译期若 `!is_base_of_v<T,U> && !is_base_of_v<U,T>` 且不可隐式转换——仍允许编译(因运行时类型可能更具体),运行时判定失败。
- **标量**:`is_convertible` 的标量对(如 int↔long)沿用现有 `can_convert`/`scalar_convertible` 语义(值转换不产生指针,故仅指针/句柄版 `as`/`cast` 处理引用类型;标量转换仍走 `convert.hpp`)。

### 类型擦除 `instance` 路径(全精度,含虚基类/多继承)

`instance` 只存静态 `typeid` + `type_desc` + 类型擦除 `data()`。全精度方案:

1. **动态类型判定**:若静态类型多态,`D = typeid(*data())`;否则 `D = 静态类型`。
2. **可转换判定**:`T` 与 `D` 相同,或 `T` 在 `D` 的反射基链中(`derived_from_desc`)。
3. **指针调整(全精度,不依赖 offset-0)**:扩展反射基链记录**偏移正确的转换 thunk**——`RTTI_DEFINE_TYPE_DERIVED(D, Base)` 编译期已知 D 与 Base,生成 `[](void* p){ return static_cast<Base*>(static_cast<D*>(p)); }` 形式的 thunk(虚基类偏移由编译器计算);沿 D→…→T 的基链路径组合 thunk 得到最终 `T*`。
   - 消除现 `dynamic_cast_ref` 的 offset-0 限制(其 KNOWN LIMITATION 在文档中保留说明历史,新 API 为全精度路径)。
4. **回退**:无法沿链组合(如未注册基链)时,若 `D` 多态可退回 `dynamic_cast`;否则失败。

### 与既有 API 的关系

- `dynamic_cast_ref<T>(instance&)` 保留(向后兼容);`instance::as<T>()`/`cast<T>()` 为全精度新路径。文档标注二者差异。
- `object_ref<T>` 的 `as`/`cast`:源静态类型 `U` 已知,优先编译期 `static_cast`(上转)/`dynamic_cast`(下转);`object_ref` 已知 `U`,无需 thunk 表。
- 原始指针版:`U` 静态已知,同样编译期 `static_cast`(上转)/`dynamic_cast`(下转)。
- 复用 `can_convert`、`derived_from_desc`、`is_base_of_v`、`is_polymorphic_v`。

### 边界与错误路径(必须全测)

- 空源:空指针 → `as`=nullptr / `cast`=no_such_field;空 `object_ref`/`instance` 同。
- 无关类型:失败码 `kind_mismatch`。
- 非多态下转:`as`=nullptr / `cast`=kind_mismatch。
- 多态下转成功:dynamic_cast 命中。
- 虚基类上转:偏移正确(解引用验证字段值)。
- 多继承:各基子对象地址正确。
- 同类型恒等。
- 二次转换/链式(`as` 结果再 `cast`)。

## 工作分解

### 阶段 1:最高优化档零告警(工作流 A)
- A-1:`summary()` 返回退出码 + 调用点改造(test_harness.hpp / test_main.cpp)。
- A-2:CMake LTO 选项联动。
- 验收:build + build-opt 全量 0 警告;四套件全绿。

### 阶段 2:cast/as + 变体(工作流 B,TDD)
- B-1:原始指针版 `as`/`cast`(reflect.hpp,自由函数)。
- B-2:`object_ref<U>` 成员 `as`/`cast`(reflection.hpp)。
- B-3:`instance` 成员 `as`/`cast` 全精度(基链 thunk;reflection.hpp + `RTTI_DEFINE_TYPE_DERIVED` 扩展)。
- B-4:测试(type_judge_test 或新 cast_test):三操作数 × 上转/下转/虚基/多继承/空/无关/同型全路径,红→绿。

### 阶段 3:双审复核 + 终验
- 独立审计(角色隔离);Debug + Release + LTCG 三档四套件全绿;README 双语同步(新增 API 文档)。

## 合规性

- 全英文注释;块注释引用 spec 章节;C++17;无第三方依赖。
- 非模板实现若体量大,按既有迁移约束入 src/*.cpp(header 声明化);默认实参留头。
- TDD:红→绿证据;C10 只跑相关 target;全量门仅收尾一次。
- 零告警硬门覆盖 Debug/Release/LTCG 三档。
