# cast/as 双向变体转换 + 最高优化档零告警 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复 LTCG/最高优化档暴露的告警使全档零警告,并新增 C# 风格 `as<T>`(失败返回空指针)/`cast<T>`(失败返回错误码)反射转换,支持三种操作数(原始指针、`object_ref<T>`、类型擦除 `instance`)沿继承层次双向转换。

**Architecture:** 在 `reflection.hpp` 的反射基链(`base_type_list`)上追加偏移正确的转换 thunk(编译期 `static_cast`;虚基类用 `dynamic_cast`),`instance` 通过运行期动态类型 + thunk 链实现全精度调整。原始指针/`object_ref` 直接用编译期 `static_cast`(上转)/`dynamic_cast`(下转)。

**Tech Stack:** C++17;MSVC 2022(`/W4 /permissive- /Zc:preprocessor`);自研极简测试 harness(`RTTI_TEST_CASE`/`RTTI_CHECK`);CMake 静态库 `rtti`。

**Spec:** `docs/superpowers/specs/2026-10-08-cast-as-variance-design.md`

## Global Constraints

- C++17;无第三方依赖;注释全英文;块注释引用 spec 章节。
- MSVC 编译选项 `/W4 /permissive- /Zc:preprocessor`;零 error 零 warning 为硬门(覆盖 Debug / Release / LTCG 三档)。
- 语义:失败安全(spec §0),禁止崩溃。
- 非模板实现若体量大入 `src/*.cpp`;默认实参留头。
- 测试只跑相关 target(C10);全量门仅收尾一次。
- 基线(Debug,权威):主 `rtti_tests` **55774/0**、`rtti_proto_extension_tests` **160/0**、`rtti_flat_schema_tests` **408/0**、剥离 `build-off` **32800/0**。
- 构建命令:`cmake` 完整路径 `"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"`;生成器 `"Visual Studio 17 2022" -A x64`;测试 exe `.\build\tests\Debug\rtti_tests.exe`。

## Review Focus

- 非多态虚基类上转:标准 C++ 无合法指针调整(既非 `static_cast` 亦非 `dynamic_cast`)→ 必须**干净失败**(`as`=nullptr / `cast`=kind_mismatch),**绝不返回错误偏移的指针**。
- `instance` 下转时动态类型的描述符未被物化(未调用过 `type_of<D>()`)→ 必须干净失败,不得崩溃/误判。
- 无关类型经 `dynamic_cast` 交叉转换(多继承,双方多态):应命中或失败,不 UB。
- `as`/`cast` 结果的 const 限定:`as<T>` 仅接受非 const 源与目标,避免 const 丢失;`object_ref<const U>` 场景不引入。
- `instance` thunk 缺失(`as_bases->adjust` 为空)时的回退:必须失败而非按 offset-0 猜测。

---

## Task 1: 最高优化档零告警(工作流 A)

**Files:**
- Modify: `tests/test_harness.hpp`
- Modify: `tests/test_main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/flat_schema_test.cpp`(仅其 `RTTI_TEST_MAIN` 用法无需改,由宏统一)

**Interfaces:**
- Produces: `int rtti_test::summary() noexcept`(返回退出码,不再 `std::exit`);`RTTI_TEST_MAIN()` 展开为 `int main() { return rtti_test::summary(); }`。

- [ ] **Step 1: 复现 LTCG 告警(证据)**

Run:
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build-opt -G "Visual Studio 17 2022" -A x64 "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /GL" "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/LTCG"
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build-opt --config Release 2>&1 | Select-String "warning"
```
Expected: 出现 `C4702`(test_main.cpp(12))与 `LNK4075`。

- [ ] **Step 2: 改造 summary() 返回退出码**

`tests/test_harness.hpp` 把:
```cpp
inline void summary() {
    std::printf("\n%d assertions, %d failures\n", assertions(), failures());
    std::fflush(stdout);
    std::exit(failures() == 0 ? 0 : 1);
}
```
改为:
```cpp
// Prints the summary and returns the process exit code (0 = pass). Callers
// return this value from main(); it does not call std::exit, so main's tail
// is not unreachable under /GL whole-program optimization (avoids C4702).
inline int summary() noexcept {
    std::printf("\n%d assertions, %d failures\n", assertions(), failures());
    std::fflush(stdout);
    return failures() == 0 ? 0 : 1;
}
```
并把宏:
```cpp
#define RTTI_TEST_MAIN() int main() { rtti_test::summary(); return 0; }
```
改为:
```cpp
#define RTTI_TEST_MAIN() int main() { return rtti_test::summary(); }
```

- [ ] **Step 3: 同步 test_main.cpp**

`tests/test_main.cpp`:
```cpp
int main() {
    // Unbuffered stdout so a crash mid-test still shows the last [TEST] line.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    return rtti_test::summary();
}
```

- [ ] **Step 4: CMake LTO 选项**

`CMakeLists.txt` 在 `option(...)` 区块后追加:
```cmake
# Interprocedural optimization (whole-program /GL + /LTCG on MSVC). Enabled for
# Release only; CMake then links with /LTCG and disables incremental linking,
# avoiding LNK4075. Default ON so the strongest optimization form is exercised.
option(RTTI_ENABLE_LTO "Enable link-time optimization in Release builds" ON)
```
在 `add_library(rtti STATIC ...)` 之后(与编译选项同一 `if(MSVC)` 前)追加:
```cmake
if(RTTI_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT RTTI_IPO_OK OUTPUT RTTI_IPO_MSG)
  if(RTTI_IPO_OK)
    set_property(TARGET rtti PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set_property(TARGET rtti PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
  else()
    message(STATUS "RTTI: IPO not supported (${RTTI_IPO_MSG}); building without LTO")
  endif()
endif()
```
并在 `add_subdirectory(tests)` / `add_subdirectory(bench)` 内每个 target 也开启(在各自 CMakeLists 末尾或根对全目标设置)。根 CMakeLists 在 `add_subdirectory(tests)` 后追加:
```cmake
if(RTTI_ENABLE_LTO AND RTTI_IPO_OK)
  foreach(_t rtti_tests rtti_proto_extension_tests rtti_flat_schema_tests)
    if(TARGET ${_t})
      set_property(TARGET ${_t} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
      set_property(TARGET ${_t} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    endif()
  endforeach()
endif()
```

- [ ] **Step 5: 重建 build-opt 验证零警告**

Run:
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build-opt -G "Visual Studio 17 2022" -A x64
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build-opt --config Release 2>&1 | Select-String "warning|error"
```
Expected: 无 `warning`/`error` 输出(空)。

- [ ] **Step 6: 三档套件回归**

Run:
```powershell
.\build-opt\tests\Release\rtti_tests.exe
.\build-opt\tests\Release\rtti_proto_extension_tests.exe
.\build-opt\tests\Release\rtti_flat_schema_tests.exe
```
Expected: 分别 `55774 assertions, 0 failures` / `160 ... 0` / `408 ... 0`。

- [ ] **Step 7: Commit**

```bash
git add tests/test_harness.hpp tests/test_main.cpp CMakeLists.txt
git commit -m "fix(build): zero-warning LTCG; summary() returns exit code"
```

---

## Task 2: 反射基链转换 thunk(全精度基础)

**Files:**
- Modify: `include/rtti/reflection.hpp`(`base_type_list` 结构、`base_adjuster`、`base_list_impl`、`adjust_to`)

**Interfaces:**
- Produces:
  - `struct base_type_list { std::size_t count; const type_resolver* resolve; void* (*const* adjust)(void*); };`
  - `detail::base_adjuster<Derived, Base>::run(void*) -> void*`(非虚基 `static_cast`;虚基且 Derived 多态用 `dynamic_cast`;虚基且非多态返回 `nullptr`)
  - `detail::adjust_to(void* ptr, const type_desc* from, const type_desc* target, std::size_t depth = 0) -> void*`

- [ ] **Step 1: 写失败测试**

在 `tests/type_judge_test.cpp` 末尾追加(使用既有 `JudgeVBase`/`JudgeVDerived`/`JudgeDerived`):
```cpp
RTTI_TEST_CASE(JudgeTestBaseAdjustThunk) {
    using namespace rtti_test_detail;
    // Non-virtual single inheritance: D -> Base offset thunk returns a usable
    // base pointer whose merged field is readable.
    JudgeDerived d;
    d.x = 7;
    d.y = 9;
    const rtti::type_desc* dd = rtti::type_of<JudgeDerived>();
    RTTI_CHECK(dd->bases() != nullptr && dd->bases()->count >= 1);
    RTTI_CHECK(dd->bases()->adjust != nullptr);
    void* bp = dd->bases()->adjust[0](static_cast<void*>(&d));
    RTTI_CHECK(bp != nullptr);
#if 0
    // Manual probe only; the assertion below pins the offset correctness.
#endif
    // The base subobject pointer must alias the base part of d (x == 7).
    RTTI_CHECK(static_cast<JudgeBase*>(bp)->x == 7);
}
```
Run:
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target rtti_tests
```
Expected: 编译失败(`base_type_list` 无 `adjust` 成员)。

- [ ] **Step 2: 扩展 base_type_list 与 base_list_impl**

`include/rtti/reflection.hpp` 把:
```cpp
struct base_type_list {
    std::size_t count;
    const type_resolver* resolve;   // array of `count` lazy resolvers
};
```
改为:
```cpp
// Offset-correct base-pointer adjuster: converts a pointer to the owning type
// into a pointer to the base subobject. Non-virtual bases use static_cast
// (correct offset, computed by the compiler); a virtual base uses dynamic_cast
// when the owning type is polymorphic and has no legal static_cast. A virtual
// base on a non-polymorphic type has no legal C++ adjustment, so the adjuster
// returns nullptr and the caller fails cleanly rather than aliasing the wrong
// memory.
using base_adjust_fn = void* (*)(void*);

struct base_type_list {
    std::size_t count;
    const type_resolver* resolve;   // array of `count` lazy resolvers
    base_adjust_fn const* adjust;   // array of `count` adjusters (parallel)
};
```

在 `detail` 命名空间内、`base_list_impl` 之前加入:
```cpp
// Detect whether static_cast<Base*>(Derived*) is well-formed. It is ill-formed
// exactly when Base is a virtual base of Derived; in that case fall back to
// dynamic_cast (requires Derived polymorphic) or fail.
template <typename Derived, typename Base, typename = void>
struct base_adjuster {
    static void* run(void* p) noexcept {
        if constexpr (std::is_polymorphic<Derived>::value) {
            return dynamic_cast<Base*>(static_cast<Derived*>(p));
        } else {
            return nullptr;  // virtual base on a non-polymorphic type: unsupported
        }
    }
};
template <typename Derived, typename Base>
struct base_adjuster<Derived, Base,
    std::void_t<decltype(static_cast<Base*>(std::declval<Derived*>()))>> {
    static void* run(void* p) noexcept {
        return static_cast<Base*>(static_cast<Derived*>(p));
    }
};
```
把 `base_list_impl` 改为同时产生 adjuster 数组(需要 Derived 类型;当前签名只接收 Bs,须加 Derived):
```cpp
template <typename Derived, typename Bs, std::size_t... I>
const base_type_list* base_list_impl(std::index_sequence<I...>) noexcept {
    static const type_resolver resolvers[sizeof...(I) > 0 ? sizeof...(I) : 1] = {
        &detail::type_of_impl<std::tuple_element_t<I, Bs>>...,
    };
    static const base_adjust_fn adjusters[sizeof...(I) > 0 ? sizeof...(I) : 1] = {
        &base_adjuster<Derived, std::tuple_element_t<I, Bs>>::run...,
    };
    static const base_type_list list{sizeof...(I), resolvers, adjusters};
    return &list;
}
```
把 `base_type_list_of` 改为传入 T:
```cpp
template <typename T>
const base_type_list* base_type_list_of() noexcept {
    using Bs = typename base_types<T>::list;
    if constexpr (std::tuple_size<Bs>::value == 0) {
        return nullptr;
    } else {
        return base_list_impl<T, Bs>(std::make_index_sequence<std::tuple_size<Bs>::value>{});
    }
}
```

- [ ] **Step 3: 增加 adjust_to 链式调整**

在 `detail` 命名空间、`derived_from_desc` 之后加入:
```cpp
// Walk the reflected base chain from `from` toward `target`, composing
// offset-correct adjusters from `ptr` (which points to the `from` subobject).
// Returns the target subobject pointer, or nullptr when the target is not
// reachable through a chain of legal adjustments. Depth-guarded like
// derived_from_desc.
inline void* adjust_to(void* ptr, const type_desc* from,
                       const type_desc* target, std::size_t depth = 0) noexcept {
    if (ptr == nullptr || from == nullptr || target == nullptr) {
        return nullptr;
    }
    if (same_type_id(from->id, target->id)) {
        return ptr;
    }
    if (depth >= 64 || from->as_bases == nullptr || from->as_bases->adjust == nullptr) {
        return nullptr;
    }
    for (std::size_t i = 0; i < from->as_bases->count; ++i) {
        const type_desc* b = from->as_bases->resolve[i]();
        if (b == nullptr) {
            continue;
        }
        void* bp = from->as_bases->adjust[i](ptr);
        if (bp == nullptr) {
            continue;
        }
        void* r = adjust_to(bp, b, target, depth + 1);
        if (r != nullptr) {
            return r;
        }
    }
    return nullptr;
}
```

- [ ] **Step 4: 运行测试**

Run:
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target rtti_tests
.\build\tests\Debug\rtti_tests.exe
```
Expected: 编译 0 警告;`55775 assertions, 0 failures`(基线 55774 + 新增约 4 断言,以实测为准,失败为 0)。

- [ ] **Step 5: Commit**

```bash
git add include/rtti/reflection.hpp tests/type_judge_test.cpp
git commit -m "feat(reflect): offset-correct base-chain adjust thunks"
```

---

## Task 3: 原始指针与 object_ref 的 as/cast

**Files:**
- Modify: `include/rtti/reflection.hpp`(自由函数 `as`/`cast`;`object_ref` 成员)
- Modify: `tests/type_judge_test.cpp`(测试)

**Interfaces:**
- Consumes: `detail::base_adjuster`(Task 2)。
- Produces:
  - `template <typename T, typename U> T* rtti::as(U* p) noexcept`
  - `template <typename T, typename U> result<T*> rtti::cast(U* p)`
  - `template <typename T> T* object_ref<U>::as() noexcept`
  - `template <typename T> result<T*> object_ref<U>::cast() noexcept`

- [ ] **Step 1: 写失败测试**

在 `tests/type_judge_test.cpp` 末尾追加:
```cpp
RTTI_TEST_CASE(JudgeTestAsCastRawPointer) {
    using namespace rtti_test_detail;
    JudgeDerived d;
    d.x = 3;
    d.y = 4;

    // Covariant upcast (derived -> base): always safe.
    JudgeBase* b = rtti::as<JudgeBase>(&d);
    RTTI_CHECK(b != nullptr && b->x == 3);

    // Exact type: identity view.
    JudgeDerived* same = rtti::as<JudgeDerived>(&d);
    RTTI_CHECK(same == &d);

    // Downcast to a type the object is not: clean failure.
    RTTI_CHECK(rtti::as<JudgeOther>(&d) == nullptr);
    auto bad = rtti::cast<JudgeOther>(&d);
    RTTI_CHECK(!bad.ok() && bad.err.code == rtti::error_code::kind_mismatch);

    // Null source.
    JudgeDerived* nullp = nullptr;
    RTTI_CHECK(rtti::as<JudgeBase>(nullp) == nullptr);
    auto nul = rtti::cast<JudgeBase>(nullp);
    RTTI_CHECK(!nul.ok() && nul.err.code == rtti::error_code::no_such_field);

    // Successful cast returns ok with the value set.
    auto up = rtti::cast<JudgeBase>(&d);
    RTTI_CHECK(up.ok() && up.value == b);
}

RTTI_TEST_CASE(JudgeTestAsCastObjectRef) {
    using namespace rtti_test_detail;
    auto h = rtti::make_object<JudgeDerived>();
    RTTI_CHECK(h.valid());
    h->x = 11;
    JudgeBase* b = h.as<JudgeBase>();
    RTTI_CHECK(b != nullptr && b->x == 11);
    RTTI_CHECK(h.as<JudgeOther>() == nullptr);
    auto c = h.cast<JudgeBase>();
    RTTI_CHECK(c.ok() && c.value == b);

    rtti::object_ref<JudgeDerived> empty;
    RTTI_CHECK(empty.as<JudgeBase>() == nullptr);
    RTTI_CHECK(empty.cast<JudgeBase>().err.code == rtti::error_code::no_such_field);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run:
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target rtti_tests
```
Expected: 编译失败(`rtti::as` 未定义)。

- [ ] **Step 3: 实现自由函数(放在 object_ref/instance 定义之前)**

在 `include/rtti/reflection.hpp`,`class object_ref` 定义**之前**加入:
```cpp
// ---------------------------------------------------------------------------
// C#-style reference conversions (spec: cast/as variance design).
//   as<T>(p)   : nullptr on failure; no error payload.
//   cast<T>(p) : result<T*> with kind_mismatch on failure and no_such_field
//                for a null source.
// Covariant upcasts use the compiler's offset-correct static_cast (virtual
// bases via dynamic_cast, see detail::base_adjuster). Contravariant downcasts
// require the static source type to be polymorphic (dynamic_cast); a
// non-polymorphic source cannot be checked at runtime and fails cleanly.
// Unrelated polymorphic types may still succeed through a dynamic_cast
// cross-cast. All entry points are failure-safe (spec section 0).
// ---------------------------------------------------------------------------

template <typename T, typename U>
T* as(U* p) noexcept {
    if (p == nullptr) {
        return nullptr;
    }
    if constexpr (std::is_base_of<T, U>::value) {
        // Covariant direction (or same type): the source statically derives
        // from the target, so the adjustment is known at compile time.
        return static_cast<T*>(detail::base_adjuster<U, T>::run(static_cast<void*>(p)));
    } else if constexpr (std::is_base_of<U, T>::value) {
        // Contravariant downcast: runtime-checked, needs a polymorphic source.
        if constexpr (std::is_polymorphic<U>::value) {
            return dynamic_cast<T*>(p);
        } else {
            return nullptr;
        }
    } else {
        // Unrelated: only a polymorphic cross-cast can succeed.
        if constexpr (std::is_polymorphic<U>::value && std::is_polymorphic<T>::value) {
            return dynamic_cast<T*>(p);
        } else {
            return nullptr;
        }
    }
}

template <typename T, typename U>
result<T*> cast(U* p) {
    result<T*> r;
    if (p == nullptr) {
        r.err.code = error_code::no_such_field;
        return r;
    }
    T* q = as<T>(p);
    if (q == nullptr) {
        r.err.code = error_code::kind_mismatch;
        return r;
    }
    r.value = q;
    return r;
}
```

- [ ] **Step 4: 增加 object_ref 成员**

在 `class object_ref` 的 `T& operator*()` 等访问器之后、`destroy()` 之前加入:
```cpp
    // C#-style variance conversions: see rtti::as / rtti::cast. `as` returns
    // nullptr on failure; `cast` returns a result with an error_code. The
    // returned pointer borrows the handle's object.
    template <typename Target>
    Target* as() noexcept {
        return rtti::as<Target>(p_);
    }
    template <typename Target>
    result<Target*> cast() noexcept {
        return rtti::cast<Target>(p_);
    }
```

- [ ] **Step 5: 运行测试**

Run:
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target rtti_tests
.\build\tests\Debug\rtti_tests.exe
```
Expected: 0 警告;新增用例通过,`0 failures`。

- [ ] **Step 6: Commit**

```bash
git add include/rtti/reflection.hpp tests/type_judge_test.cpp
git commit -m "feat(reflect): as/cast for raw pointers and object_ref"
```

---

## Task 4: instance 全精度 as/cast(类型擦除路径)

**Files:**
- Modify: `include/rtti/reflection.hpp`(`instance::ops` 扩展、`ops_impl` 扩展、类型注册表 `detail::register_desc`/`find_desc`、`instance::as`/`cast`)
- Modify: `tests/type_judge_test.cpp`(测试)

**Interfaces:**
- Consumes: `detail::adjust_to`(Task 2)、`rtti::as`/`cast`(Task 3)。
- Produces:
  - `detail::register_desc(const type_desc*) noexcept`
  - `detail::find_desc(const std::type_info&) noexcept -> const type_desc*`
  - `template <typename T> T* instance::as() noexcept`
  - `template <typename T> result<T*> instance::cast() noexcept`

- [ ] **Step 1: 写失败测试**

在 `tests/type_judge_test.cpp` 末尾追加:
```cpp
RTTI_TEST_CASE(JudgeTestInstanceAsCast) {
    using namespace rtti_test_detail;
    // Force the derived descriptors to be materialized/registered.
    (void)rtti::type_of<JudgeDerived>();
    (void)rtti::type_of<JudgeDeep>();

    auto h = rtti::make_object<JudgeDeep>();
    h->x = 1;
    h->y = 2;
    h->z = 3;
    rtti::instance inst = rtti::instance::from(std::move(h));

    // Upcast through two levels (Deep -> Derived -> Base) with correct offsets.
    JudgeBase* b = inst.as<JudgeBase>();
    RTTI_CHECK(b != nullptr && b->x == 1);
    JudgeDerived* d = inst.as<JudgeDerived>();
    RTTI_CHECK(d != nullptr && d->y == 2);
    // Exact dynamic type.
    RTTI_CHECK(inst.as<JudgeDeep>() != nullptr);
    // Unrelated: clean failure.
    RTTI_CHECK(inst.as<JudgeOther>() == nullptr);
    RTTI_CHECK(inst.cast<JudgeOther>().err.code == rtti::error_code::kind_mismatch);
    // Successful cast.
    auto c = inst.cast<JudgeDerived>();
    RTTI_CHECK(c.ok() && c.value == d);

    // Empty handle.
    rtti::instance empty;
    RTTI_CHECK(empty.as<JudgeBase>() == nullptr);
    RTTI_CHECK(empty.cast<JudgeBase>().err.code == rtti::error_code::no_such_field);
}

RTTI_TEST_CASE(JudgeTestInstanceDowncastPolymorphic) {
    using namespace rtti_test_detail;
    (void)rtti::type_of<JudgePolyD>();
    // Store a derived object behind a base handle, then recover the derived
    // view through the type-erased instance (downcast).
    rtti::object_ref<JudgePoly> base_handle = rtti::object_ref<JudgePoly>::adopt(new JudgePolyD());
    JudgePolyD* raw = dynamic_cast<JudgePolyD*>(base_handle.get());
    RTTI_CHECK(raw != nullptr);
    raw->pd = 42;
    rtti::instance inst = rtti::instance::from(std::move(base_handle));
    JudgePolyD* rec = inst.as<JudgePolyD>();
    RTTI_CHECK(rec != nullptr && rec->pd == 42);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run:
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target rtti_tests
```
Expected: 编译失败(`instance::as` 未定义)。

- [ ] **Step 3: 类型描述符注册表**

在 `include/rtti/reflection.hpp` 的 `detail` 命名空间内(`adjust_to` 之后;需要 `<vector>` 与 `<mutex>`,阻 `<mutex>` 已在?若未 include 则加入 include 区)添加:
```cpp
// Process-wide registry of reflected type descriptors, keyed by std::type_info
// value. Populated lazily the first time type_of<T>() materializes a reflected
// descriptor; used by the type-erased instance cast to resolve a runtime
// dynamic type to its descriptor (base chain + adjusters). Thread-safe.
inline std::vector<const type_desc*>& desc_registry() {
    static std::vector<const type_desc*> v;
    return v;
}
inline std::mutex& desc_registry_mutex() {
    static std::mutex m;
    return m;
}
inline void register_desc(const type_desc* d) noexcept {
    if (d == nullptr || d->id == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lk(desc_registry_mutex());
    for (const type_desc* e : desc_registry()) {
        if (e != nullptr && e->id != nullptr && *e->id == *d->id) {
            return;
        }
    }
    desc_registry().push_back(d);
}
inline const type_desc* find_desc(const std::type_info& ti) noexcept {
    std::lock_guard<std::mutex> lk(desc_registry_mutex());
    for (const type_desc* e : desc_registry()) {
        if (e != nullptr && e->id != nullptr && *e->id == ti) {
            return e;
        }
    }
    return nullptr;
}
```
(若 `reflection.hpp` 未包含 `<mutex>`,在 include 区加 `#include <mutex>`。)

- [ ] **Step 4: 在 type_of_impl 的 reflected 分支注册描述符**

把 `type_of_impl<T>` 的 reflected 分支:
```cpp
    } else if constexpr (is_reflected<T>::value) {
        static const struct_desc s{field_count<T>(), fields_of<T>::desc};
        static const type_desc d{type_kind::reflected, &typeid(T), typeid(T).name(),
                                 nullptr, &s, nullptr,
                                 detail::base_type_list_of<T>(),
                                 std::is_abstract<T>::value,
                                 std::is_polymorphic<T>::value};
        return &d;
```
改为在 `return &d;` 前注册一次:
```cpp
        static const bool registered = (detail::register_desc(&d), true);
        (void)registered;
        return &d;
```

- [ ] **Step 5: 扩展 instance 的 ops 与 ops_impl**

把 `struct ops` 增加两个函数指针:
```cpp
    struct ops {
        error_code (*get)(void* obj, const char* name, field_value& out) noexcept;
        error_code (*set)(void* obj, const char* name, const field_value& v) noexcept;
        error_code (*to)(const void* obj, value_io* dst);
        error_code (*from)(void* obj, const value_io* src);
        void (*destroy)(void* obj) noexcept;
        void* (*storage)(void* obj) noexcept;
        void* (*most_derived)(void* obj) noexcept;             // dynamic_cast<void*>
        const std::type_info* (*dynamic_type)(void* obj) noexcept;
    };
```
在 `ops_impl<T>` 增加:
```cpp
        static void* most_derived(void* obj) noexcept {
            T* p = static_cast<object_ref<T>*>(obj)->get();
            if (p == nullptr) {
                return nullptr;
            }
            if constexpr (std::is_polymorphic<T>::value) {
                return dynamic_cast<void*>(p);
            } else {
                return static_cast<void*>(p);
            }
        }
        static const std::type_info* dynamic_type(void* obj) noexcept {
            T* p = static_cast<object_ref<T>*>(obj)->get();
            if (p == nullptr) {
                return nullptr;
            }
            if constexpr (std::is_polymorphic<T>::value) {
                return &typeid(*p);
            } else {
                return &typeid(T);
            }
        }
```
把 `ops_impl<T>` 的表初始化:
```cpp
        static inline const ops table = {get, set, to, from, destroy, storage};
```
改为:
```cpp
        static inline const ops table = {get, set, to, from, destroy, storage,
                                         most_derived, dynamic_type};
```

- [ ] **Step 6: 实现 instance::as/cast**

在 `class instance` 的 `destroy()` 之后、`private:` 之前加入:
```cpp
    // C#-style variance conversions through the type-erased handle. Full
    // precision: the runtime dynamic type (when the stored type is
    // polymorphic) resolves to its reflected descriptor, and the base chain's
    // offset-correct adjusters compute the target subobject. A downcast whose
    // dynamic descriptor was never materialized (type_of<D>() never called)
    // falls back to the static chain, which only reaches bases. Failures
    // return nullptr / error_code, never a wrong pointer (spec section 0).
    template <typename T>
    T* as() noexcept {
        if (!valid()) {
            return nullptr;
        }
        const type_desc* target = type_of<T>();
        if (target == nullptr) {
            return nullptr;
        }
        const std::type_info* dt = op_->dynamic_type(obj_);
        const type_desc* root = type_of_;
        void* root_ptr = data();
        if (dt != nullptr && type_of_ != nullptr && type_of_->id != nullptr &&
            *dt != *type_of_->id) {
            const type_desc* dd = detail::find_desc(*dt);
            if (dd != nullptr) {
                root = dd;
                root_ptr = op_->most_derived(obj_);
            }
        }
        return static_cast<T*>(detail::adjust_to(root_ptr, root, target));
    }

    template <typename T>
    result<T*> cast() noexcept {
        result<T*> r;
        if (!valid()) {
            r.err.code = error_code::no_such_field;
            return r;
        }
        T* q = as<T>();
        if (q == nullptr) {
            r.err.code = error_code::kind_mismatch;
            return r;
        }
        r.value = q;
        return r;
    }
```

- [ ] **Step 7: 运行测试**

Run:
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target rtti_tests
.\build\tests\Debug\rtti_tests.exe
```
Expected: 0 警告;`0 failures`。

- [ ] **Step 8: Commit**

```bash
git add include/rtti/reflection.hpp tests/type_judge_test.cpp
git commit -m "feat(reflect): full-precision instance as/cast via dynamic type chain"
```

---

## Task 5: 文档、边界补测与终验

**Files:**
- Modify: `README.en.md`,`README.zh-CN.md`(新增 cast/as 章节,中英镜像)
- Modify: `tests/type_judge_test.cpp`(虚基/多态边界补测)
- Test: 全量三档

**Interfaces:**
- Consumes: 前四任务全部 API。

- [ ] **Step 1: 虚基/多态边界补测**

在 `tests/type_judge_test.cpp` 末尾追加:
```cpp
RTTI_TEST_CASE(JudgeTestAsCastVirtualBase) {
    using namespace rtti_test_detail;
    // Non-polymorphic virtual base: no legal pointer adjustment -> clean fail.
    JudgeVDerived vd;
    vd.vx = 5;
    vd.vy = 6;
    RTTI_CHECK(rtti::as<JudgeVBase>(&vd) == nullptr);
    RTTI_CHECK(rtti::cast<JudgeVBase>(&vd).err.code == rtti::error_code::kind_mismatch);

    }
```
说明:非多态源的非虚基上转已在 `JudgeTestAsCastRawPointer`(JudgeDerived→JudgeBase)覆盖;本用例专测非多态虚基必须干净失败。

- [ ] **Step 2: 运行测试**

Run:
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target rtti_tests
.\build\tests\Debug\rtti_tests.exe
```
Expected: `0 failures`。

- [ ] **Step 3: README 双语章节**

在 `README.en.md` 与 `README.zh-CN.md` 各新增一节 "Variance casts / 变体转换",中英逐条对应:
```markdown
## Variance casts

`rtti::as<T>` and `rtti::cast<T>` mirror C# reference conversions:

- `as<T>(x)` returns `T*` or `nullptr` when the conversion is not possible.
- `cast<T>(x)` returns `result<T*>`; a failed conversion sets
  `error_code::kind_mismatch` (a null source sets `no_such_field`).

They accept a raw pointer, an `object_ref<U>`, or a type-erased `instance`.
Covariant upcasts (derived to base) are compile-time correct, including
virtual bases when the source is polymorphic. Contravariant downcasts are
runtime-checked and require a polymorphic source; a non-polymorphic source
fails cleanly. A virtual base of a non-polymorphic type has no legal pointer
adjustment and also fails cleanly rather than aliasing the wrong memory.
```
```markdown
## 变体转换

`rtti::as<T>` 与 `rtti::cast<T>` 对应 C# 的引用转换:

- `as<T>(x)` 返回 `T*`,无法转换时返回 `nullptr`。
- `cast<T>(x)` 返回 `result<T*>`;转换失败设 `error_code::kind_mismatch`
  (空源设 `no_such_field`)。

三者均支持:原始指针、`object_ref<U>`、类型擦除的 `instance`。协变上转
(派生到基)编译期正确,源多态时含虚基类。逆变下转运行时检查,要求源多态;
非多态源干净失败。非多态类型的虚基类无合法指针调整,同样干净失败,绝不
返回错误偏移的指针。
```

- [ ] **Step 4: 全量三档终验**

Run:
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug 2>&1 | Select-String "warning|error"
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Release 2>&1 | Select-String "warning|error"
& "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build-opt --config Release 2>&1 | Select-String "warning|error"
```
Expected: 三条均**无输出**(0 警告 0 错误)。

Run:
```powershell
.\build\tests\Debug\rtti_tests.exe
.\build\tests\Debug\rtti_proto_extension_tests.exe
.\build\tests\Debug\rtti_flat_schema_tests.exe
.\build-off\tests\Debug\rtti_tests.exe
```
Expected: 主 `rtti_tests` = 基线 55774 + 本计划新增断言;`proto_ext` 160/0;`flat_schema` 408/0;`build-off` 32800/0 + 新增核心断言。失败均为 0。

- [ ] **Step 5: Commit**

```bash
git add README.en.md README.zh-CN.md tests/type_judge_test.cpp
git commit -m "docs: cast/as variance (bilingual); virtual-base edge tests"
```

---

## Self-Review

- **Spec coverage**: 工作流 A(Task 1)、原始指针/object_ref(Task 3)、instance 全精度(Task 4)、双向变体规则(Task 2+3+4)、边界与错误路径(Task 3/4/5)、文档双语(Task 5)。全部覆盖。
- **Placeholder scan**: 无 TBD;所有步骤含可执行代码/命令。
- **Type consistency**: `base_adjust_fn = void*(*)(void*)`、`base_type_list::adjust`、`base_adjuster<Derived,Base>::run`、`adjust_to(ptr,from,target,depth)`、`register_desc`/`find_desc`、`instance::ops::most_derived`/`dynamic_type`、`as<T>(U*)`/`cast<T>(U*)` 在各任务间命名与签名一致。
- **Review Focus**: 非多态虚基(Task 5 Step 1)、未物化动态描述符(Task 4 Step 1 空/回退路径)、多态交叉转换(Task 3 无关分支)、const(仅非 const)、thunk 缺失回退(Task 2 `adjust==nullptr` 守卫)。均有测试或守卫。