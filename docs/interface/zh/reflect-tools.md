# 反射工具集 — 接口参考(reflect / ioc / to_string / str)

反射核心之上的工具层:运行时查询层(`<rtti/reflect.hpp>`)、依赖注入容器
(`<rtti/ioc.hpp>`)、统一 `T <-> std::string` 转换层(`<rtti/to_string.hpp>`)
与字符串操作工具集(`<rtti/string_ops.hpp>`)。本文档为中英双语并互索;
英文版见 [`en/reflect-tools.md`](../en/reflect-tools.md)。

- [English (英文)](../en/reflect-tools.md)

---

## 0. 范围

| 头文件 | 命名空间 | 本文档章节 |
| --- | --- | --- |
| `<rtti/reflect.hpp>` | `rtti` | §1–§3 |
| `<rtti/ioc.hpp>` | `rtti::ioc` | §4 |
| `<rtti/to_string.hpp>` | `rtti` | §5 |
| `<rtti/string_ops.hpp>` | `rtti::str` | §6 |

`reflect.hpp` 是 `reflection.hpp` 中 `fields_of<T>` 之上的薄便捷层:类型自省、
按名查字段、类型化读写、运行时容器操作、有界检查的字段/容器元素迭代器与
反射实例化。`ioc.hpp` 在反射实例化层之上构建依赖注入容器。`to_string.hpp`
通过一对入口将任意受支持类型与 `std::string` 互转。`string_ops.hpp` 在
`rtti::str` 命名空间下再导出该层并追加纯字符串变换。

### 0.1 共享约定(spec §0)

- **失败即安全**:每个运行时入口在失败时返回 `error_code`、`result`、`false`、
  空指针或空值。查询层不抛异常、不崩溃。
- `noexcept`:所有不会失败的函数均标 `noexcept`;对象构造(`make_object`)
  是唯一分配面,通过 `error_code` 报告失败。
- 有界迭代器绝不形成或解引用 before-begin / one-past-end 指针(spec §0.2;
  见本文档 §2)。

---

## 1. `reflect.hpp` — 运行时查询层

### 1.1 编译期类型判定便捷项

标准 trait 的 `rtti` 命名空间别名,使调用方保持在同一命名空间内(spec §19.2)。

| 实体 | 行为 | reflect.hpp |
| --- | --- | --- |
| `template<class T, class U> struct is_same` | `std::is_same<T, U>` | :22 |
| `is_same_v` | — | :25 |
| `template<class Base, class Derived> struct is_base_of` | `std::is_base_of<Base, Derived>` | :27 |
| `is_base_of_v` | — | :30 |
| `template<class From, class To> struct is_convertible` | `std::is_convertible<From, To>` | :32 |
| `is_convertible_v` | — | :35 |
| `template<class From, class To> struct can_cast` | `std::is_convertible<From, To>`(派生/可转读法) | :38 |
| `can_cast_v` | — | :41 |

### 1.2 运行时类型判定

```cpp
template <typename T> bool is_instance_of(const void* obj, type_id expected) noexcept;
template <typename T> bool is(const instance& inst) noexcept;
template <typename T> bool is_derived_from(const instance& inst) noexcept;
```

- `is_instance_of<T>` — `obj` 处的对象是否为 `expected` 的实例。多态静态类型
  `T` 时 `typeid(*obj)` 在运行时解析动态类型;非多态 `T` 时为静态比较。空参数
  从不属于任何类型。
- `is<T>(inst)` — 实例句柄的精确运行时类型检查(type_of 相等,
  `instance::is<T>`)。
- `is_derived_from<T>(inst)` — 实例的运行时类型为 `T` 或沿反射基类链传递派生自
  `T`(`detail::derived_from_desc`)。无效句柄返回 `false`。

```cpp
template <typename T> result<T*> dynamic_cast_ref(instance& inst) noexcept;
```

同类型或经 offset-0(非虚、主基类)基链派生自 `T` → `ok()`,返回的指针借用
实例对象(生命周期 == 句柄生命周期)。无关或目标为基类的返回 `kind_mismatch`
与空指针。**已知限制**(由 `JudgeTestDynamicCastRefVirtualBaseKnownLimit`
钉定):返回指针为 `static_cast<T*>(inst.data())` —— 仅当非虚单继承链且基类
位于偏移 0 时有效。`RTTI_DEFINE_TYPE_DERIVED` + 虚基类或次基类为
`dynamic_cast_ref` 不支持的组合;它仍报 `ok()`,故该视图禁止解引用。

```cpp
bool can_convert(const type_desc* src, const type_desc* target) noexcept;
bool can_convert(const instance& inst, const type_desc* target) noexcept;
```

`can_convert(desc, desc)` 在类型 id 相同、派生关系成立或标量可隐式转换
(`detail::scalar_convertible`)时为真。`instance` 重载对无效句柄与空目标返回
`false`。

### 1.3 `type_info<T>` — .NET 级自省句柄

```cpp
template <typename T>
struct type_info {
    using value_type = T;
    static constexpr const char* name() noexcept;              // typeid(T).name()
    static constexpr std::size_t field_count() noexcept;       // rtti::field_count<T>()
    static const field_access_base* const* fields() noexcept;  // rtti::fields<T>()
    static const field_access_base* find_field(const char* field_name) noexcept;
};
template <typename T> type_info<T> get_type() noexcept;                  // 类似 typeof
template <typename T> type_info<std::decay_t<T>> get_type(const T&) noexcept;
```

`name()` 返回 `typeid(T).name()`;`find_field` 对字段表做 `std::strcmp` 遍历,
名字缺失或参数为空返回 `nullptr`。`get_type(T)` 对照 `typeof(T)`,
`get_type(obj)` 对照 `obj.GetType()`。

### 1.4 按名访问字段

```cpp
template <typename T> const field_access_base* find_field(const char* name) noexcept;
template <typename T> bool get_field(const T& obj, const char* name, field_value& out) noexcept;
template <typename T> bool set_field(T& obj, const char* name, const field_value& in) noexcept;
```

- `find_field<T>` — 命名字段访问器;无反射字段匹配时返回 `nullptr`(不抛、无
  UB)。
- `get_field` — 读字段到 `out`(`f->get`);名字不匹配时返回 `false`。
- `set_field` — `f->set(obj, in)`;字段缺失或访问器拒绝类型时返回 `false`
  (`field_value` 自带 kind,kind 不匹配由访问器检出)。

### 1.5 运行时容器操作

```cpp
template <typename T> error_code add_field(T& obj, const char* name, const value_io* element) noexcept;
template <typename T> error_code add_field(T& obj, const char* name, const field_value& element) noexcept;
template <typename T> error_code erase_field_at(T& obj, const char* name, std::size_t index) noexcept;
template <typename T> error_code erase_field_key(T& obj, const char* name, string_view key) noexcept;
template <typename T> error_code clear_field(T& obj, const char* name) noexcept;
template <typename T> std::size_t field_size(const T& obj, const char* name, error_code* out_err = nullptr) noexcept;
template <typename T, typename Fn>
error_code for_each_element(const T& obj, const char* name, value_io* scratch, Fn&& fn);
```

错误映射(所有入口):未知字段 → `no_such_field`;字段非容器 → `kind_mismatch`;
成功 → `ok`。

- `add_field` — 向容器字段追加一个元素。`value_io*` 重载从调用方 value_io
  源读取元素;`field_value` 重载用于标量元素容器(`vector`/`list`/`deque` 的
  算术/字符串、`set`、`queue`/`stack`/`priority_queue`)。map 字段必须用
  `value_io` 形式。
- `erase_field_at` — 删除序列字段第 `index` 个元素。
- `erase_field_key` — 按键从 map/set 字段删除一个元素。
- `clear_field` — 清空容器字段。
- `field_size` — 容器字段元素个数。字段非容器或未知时,`*out_err`(可空)收到
  错误并返回 `0`;成功时非空则置 `ok`。
- `for_each_element` — 访问每个元素。访问器收到元素下标、键(非关联容器为空)
  与写入 `scratch` 的 `value_io` 节点。访问器返回 `false` 提前停止遍历。

```cpp
// 标量示例:累加每个元素的整数值
std::int64_t total = 0;
rtti::for_each_element(box, "items", &scratch,
    [&](std::size_t i, rtti::string_view key, const rtti::value_io* v) {
        std::int64_t n = 0;
        if (v->as_integer(n)) {
            total += n;
        }
        return i < 10; // 十个元素后停止
    });
```

*`value_io` 协议(kind、`as_*`、`make_*`)详见 `convert.md` §2.1 与
`reflection.md` §3.4。*

---

## 2. 字段数组与容器元素迭代器

### 2.1 `field_iterator<T>`(spec §10.2)

```cpp
template <typename T> class field_iterator {
    using field_type   = const field_access_base*;
    using owner_type   = typename std::decay<T>::type;
    std::size_t index() const noexcept;
    bool is_reverse() const noexcept;
    field_type operator*() const noexcept;    // 边界位置返回 nullptr
    field_type operator->() const noexcept;
    field_iterator& operator++() noexcept;    // 前置/后置自增自减
    field_iterator operator++(int) noexcept;
    field_iterator& operator--() noexcept;
    field_iterator operator--(int) noexcept;
    bool operator==(const field_iterator& o) const noexcept;
    bool operator!=(const field_iterator& o) const noexcept;
};
template <typename T> field_iterator<T> field_begin() noexcept;  // (index 0)
template <typename T> field_iterator<T> field_end() noexcept;    // (index field_count)
template <typename T> field_iterator<T> field_rbegin() noexcept; // (index field_count, 反向)
template <typename T> field_iterator<T> field_rend() noexcept;   // (index 0, 反向)
template <typename T> field_iterator<T> begin() noexcept;  // == field_begin<T>
template <typename T> field_iterator<T> end() noexcept;    // == field_end<T>
template <typename T> field_iterator<T> rbegin() noexcept; // == field_rbegin<T>
template <typename T> field_iterator<T> rend() noexcept;   // == field_rend<T>
```

迭代器通过 `(index, reverse)` 视图化反射字段表,绝不直接持原始数组指针。
每次解引用都在表界内:`end`、`rend` 与默认构造的迭代器返回空访问器,而不会
形成 before-begin / one-past-end 指针。前向迭代器从 index 0 开始,`++`
向 `field_count` 移动;反向迭代器从 `field_count`(`rbegin`)开始,`++` 向 0
移动(解引用 `index-1`,故在扫描在 `rend` 结束前先访问首字段)。表存
`const field_access_base*`,一个迭代器类型同时覆盖 const 与非 const `T`。

```cpp
for (auto it = rtti::field_begin<Point>(); it != rtti::field_end<Point>(); ++it) {
    const rtti::field_access_base* f = *it;   // 越过末尾时 f == nullptr
    ...
}
for (auto it = rtti::rbegin<Point>(); it != rtti::rend<Point>(); ++it) { ... } // 逆序
```

### 2.2 `element_type<T>` 与 `element_iterator<T>`(spec §10.3/§10.4)

```cpp
template <typename T> const type_desc* element_type(const char* name) noexcept;
```

字段表 `type_descs` 解析出的容器元素类型。字段缺失或非容器时返回 `nullptr`。
有意的规格偏离:返回 `const type_desc*`(与 `type_of` 一致,避免拷贝成员为
指针的描述符),而非按值 `type_desc`。

```cpp
template <typename T> class element_iterator {
    using owner_type = typename std::decay<T>::type;
    const field_access_base* field() const noexcept;
    const owner_type* owner() const noexcept;
    bool is_reverse() const noexcept;
    std::size_t index() const noexcept;          // 逻辑 0 基位置
    bool at_begin() const noexcept;
    bool at_end() const noexcept;
    element_iterator& operator++() noexcept;      // 前置/后置,均钳制
    element_iterator operator++(int) noexcept;
    element_iterator& operator--() noexcept;
    element_iterator operator--(int) noexcept;
    field_value element_value() const noexcept;  // 失败时空/object 标记
    error_code  element_to(value_io* scratch) const noexcept;
    string_view key() const noexcept;
};
template <typename T>
error_code element_begin(const T& obj, const char* name, element_iterator<T>& out) noexcept;
template <typename T>
error_code element_end(const T& obj, const char* name, element_iterator<T>& out) noexcept;
template <typename T>
error_code element_rbegin(const T& obj, const char* name, element_iterator<T>& out) noexcept;
template <typename T>
error_code element_rend(const T& obj, const char* name, element_iterator<T>& out) noexcept;
```

迭代器持有所有者对象与字段访问器,故对象存活期间保持有效;每次解引用前重新
读取并边界检查实时容器大小(spec §0)。

- `element_value()` — 标量/字符串元素值;越界返回空 `field_value`(kind
  `none`)。嵌套/容器元素返回 `field_value::of_object()`。字符串结果指向迭代器
  自有的缓冲,在迭代器推进或销毁前有效。
- `element_to(scratch)` — 经格式无关的 `value_io` 路径写元素。越界解引用返回
  `no_such_field`(`error.hpp` 无 `out_of_range` 码,以 `no_such_field` 表达
  "该元素不存在")。
- `key()` — 关联容器的元素键;`set` 以其值作键(数值用十进制形式;实数使用
  共享的最短往返 `detail::format_real`,而非 `std::to_string` 的固定 6 位)。
  序列字段或越界返回空视图。

### 2.3 容器元素 range-for

```cpp
rtti::element_iterator<Box> it;
for (rtti::element_begin(box, "nums", it);
     !it.at_end();
     ++it) {
    std::int64_t n = 0;
    it.element_to(&scratch);              // 或 it.element_value().as_integer()
}
```

---

## 3. 反射构造与 ORM 绑定(spec §18)

```cpp
template <typename T> object_ref<T> make_object();                             // RAII, new T()
template <typename T> result<object_ref<T>> make_object(const ctor_arg* args, std::size_t count);
template <typename T> result<object_ref<T>> make_object(std::initializer_list<ctor_arg> args);
```

`make_object<T>()` 默认构造反射类型 `T` 并返回 RAII 模式(自动释放)的
`object_ref<T>`。`args` 重载经注册的构造器表(`ctor_registry<T>`,由
`RTTI_DEFINE_CTOR` 填充)构造 `T`:`ctor_prepare` 把名字/位置实参映射到参数
包,再由 `ctor_new_impl` 执行定位构造。任何注册表/实参错误(无工厂、`count`
> 0 但 `args` 为空、缺必需参数、kind/类型不匹配)时 `result` 携带
`error_code` — `kind_mismatch` / `missing_key` — 且永不抛异常、不崩溃(spec
§0)。

### 3.1 自定义分配/释放(spec §18.2 placement new)

```cpp
template <typename T, typename Alloc,
          std::enable_if_t<detail::is_raw_allocator_v<Alloc>, int> = 0>
object_ref<T> make_object(Alloc& alloc);                                              // T() placement
template <typename T, typename Alloc, std::enable_if_t<...> = 0>
object_ref<T> make_object(Alloc& alloc, const ctor_arg* args, std::size_t count);
template <typename T, typename Alloc, std::enable_if_t<...> = 0>
object_ref<T> make_object(Alloc& alloc, std::initializer_list<ctor_arg> args);
```

`Alloc` 经 `memory_arena` 风格 `alloc(size, align)` 或 `std::pmr` 风格
`allocate(size, align)`(`detail::has_alloc_bytes` / `has_allocate_bytes` 检测)
提供裸内存。`T` 定位构造进该内存,句柄运行在 **MANUAL** 模式:调用方必须显式
调用 `object_ref::destroy()`(无自动释放)。两模式由构造互斥,杜绝双重释放。
注册表拒绝实参(未分配)或分配器返回空时返回空句柄。

释放语义(`detail::placement_releaser`):可复制分配器被快照进释放闭包,故
`destroy()` 在调用方分配器对象已离开作用域后仍有效(R2-3)。不可复制分配器
(pmr `memory_resource`)遵循其标准契约 — 资源必须存活至其供应的所有分配结束
(闭包持引用)。arena 风格分配器不暴露逐对象释放,其闭包为 no-op(内存由所有者
批量释放)。

### 3.2 ORM 契约(spec §18.2)

```cpp
template <typename T> result<object_ref<T>> create_instance(const ctor_arg* args, std::size_t count);
template <typename T> result<object_ref<T>> create_instance(std::initializer_list<ctor_arg> args);

struct row_cell { const char* name; field_value value; };   // 一个 Bind 行单元

template <typename T>
error_code bind(object_ref<T>& obj, const row_cell* row, std::size_t count) noexcept;
template <typename T>
error_code bind(object_ref<T>& obj, std::initializer_list<row_cell> row) noexcept;
```

`create_instance<T>(args, count)` 即 `make_object<T>(args, count)`
(`CreateInstance` 契约)。`bind<T>` 按名把结果集行应用到实例的反射字段
(`object_ref::set`);`row` 空且 `count > 0` 为 `kind_mismatch`,每个失败单元
终止扫描并返回首个 `no_such_field` / `kind_mismatch`,已绑定字段保留在实例上。

```cpp
rtti::result<rtti::object_ref<Point>> r = rtti::create_instance<Point>({
    { "x", rtti::field_value::of(std::int64_t(3)) },
    { "y", rtti::field_value::of(std::int64_t(4)) },
});
assert(r.ok());
```

---

## 4. `ioc.hpp` — 依赖注入容器

`rtti::ioc::container` 通过反射实例化层(spec §18)构建实例并管理其生命周期。
容器拥有它创建的每个单例,并在析构时释放(RAII);瞬态实例以调用方所有的
`object_ref<T>` 交出。

### 4.1 实例生命周期策略

```cpp
namespace rtti::ioc {
enum class lifetime {
    transient, // 每次 resolve 全新实例;调用方持有句柄
    singleton  // 仅创建一次并复用;容器持有实例
};
}
```

瞬态:每次 `resolve` 构建全新实例;调用方拥有 `object_ref<T>` 句柄。单例:仅
创建一次、在容器生命周期内复用。

**单例借用句柄**是非持有视图。对之调用 `destroy()` 属调用方误用但失败即安全
(spec §0):被销毁的单例被标记为失效,下一次 `resolve<T>` 重新创建 — 容器从
不交出 use-after-free 实例,也绝不运行两次析构(失效单例的块被释放时不二次
执行 `~T()`)。借用句柄只持有存活标志(`detail::guarded_singleton` + 按值捕获
进删除器的共享 `live` 标志),故该契约仅在容器存活期间成立:借用句柄不得比
容器活得更久。

### 4.2 注册

```cpp
template <typename T>    void register_type(lifetime lf = lifetime::singleton);
template <typename T>    void register_type(const char* name, lifetime lf);
template <typename Iface, typename Impl> void register_as(lifetime lf = lifetime::singleton);
template <typename Iface, typename Impl> void register_as(const char* name, lifetime lf);
template <typename T>
void register_factory(std::function<std::unique_ptr<T>(container&)> factory,
                      lifetime lf = lifetime::singleton);
template <typename T>
void register_factory(const char* name,
                      std::function<std::unique_ptr<T>(container&)> factory,
                      lifetime lf);
```

- `register_type<T>` — 按类型(可选名字)注册可解析的 `T`。`resolve<T>()` 以其
  反射构造器(依赖注入)构建,无构造器注册表时默认构造。
- `register_as<Iface, Impl>` — 把接口/抽象基类映射到具体实现。`static_assert
  (std::is_base_of<Iface, Impl>)`;`resolve<Iface>` 以 `Iface` 视角返回 `Impl`
  实例。映射要求 offset-0 基类的单继承链(plain `static_cast`);
  `resolve` 瞬态路径经 `object_ref<T>` 删除,故接口应声明**虚析构**以保证删除
  良定义。
- `register_factory<T>` — 注册工厂 `std::function<std::unique_ptr<T>(container&)>`;
  工厂收到容器,故可解析自身依赖。工厂返回空报告 `kind_mismatch`。

注册键为 `(std::type_index, name)`;`name != nullptr` 消解同一 C++ 类型的多个
条目。依赖查找的键为剥离后的基类型(`detail::dep_target` 去掉指针/引用/
const 与 `std::shared_ptr`)。

### 4.3 解析与查询

```cpp
template <typename T> result<object_ref<T>> resolve();            // == resolve<T>(nullptr)
template <typename T> result<object_ref<T>> resolve(const char* name);
template <typename T> result<object_ref<T>> get();               // == resolve<T>()
template <typename T> result<object_ref<T>> get(const char* name);
template <typename T> bool registered() const noexcept;
std::size_t count() const noexcept;
```

`resolve<T>`:未注册 → `kind_mismatch`;重入环 → `kind_mismatch`(容器维护在途
类型栈;同一类型的重入被拒绝而非无限递归)。单例条目返回借用视图(见 §4.1);
瞬态条目返回调用方所有的句柄。

### 4.4 依赖注入

构造参数中底层类型(`detail::dep_target`)匹配已注册依赖的,自动注入。默认支持
的形态:

| 参数 `D` | 注入 |
| --- | --- |
| `X`, `X const&` / `X*`(借用) | 借用/共享容器所有的**单例** |
| `std::shared_ptr<X>`(共享) | 进入单例控制块的 aliasing `shared_ptr` |
| 纯 `X`(值) | 复制单例,或移动全新瞬态实例 |
| 非依赖标量 | optional 参数值初始化(`D{}`);必需缺失 → `missing_key` |

借用/共享*瞬态*依赖为 `kind_mismatch`(无可复用);纯 `X` 对瞬态工厂创建全新
实例并移动进构造实参。注入同样触发环检测:已在在途栈上的类型的重入为
`kind_mismatch`。

```cpp
struct Engine { double power = 0.0; };                                // 已反射
struct Car {
    Engine engine; std::int64_t seats = 0;
    Car() = default;
    Car(Engine e, std::int64_t s) : engine(e), seats(s) {}
};
RTTI_DEFINE_CTOR(Car, ("engine", Engine, false), ("seats", std::int64_t, true))

rtti::ioc::container c;
c.register_type<Engine>();          // 默认单例
c.register_type<Car>();

auto re = c.resolve<Engine>();      // 单例借用视图
assert(re.ok() && re.value->set("power", rtti::field_value::of(350.0)) == rtti::error_code::ok);

rtti::result<rtti::object_ref<Car>> rc = c.resolve<Car>();
assert(rc.ok());                    // "engine" 参数由 DI 注入;"seats" optional -> 0
assert(re.value->get() != nullptr); // 容器存活期内借用视图有效
rtti::field_value seats;
assert(rtti::get_field(*rc.value->get(), "seats", seats) && seats.as_integer() == 0);
```

构造参数与 `make_object` 共用同一 `ctor_registry<T>` / `ctor_prepare` 机制:
参数名来自 `RTTI_DEFINE_CTOR` 三元组,DI 在 optional 默认值之前填充依赖。
无注册构造器注册表时容器回退到 `new T()`,此路径不发生注入。

---

## 5. `to_string.hpp` — 统一 `T <-> std::string`

每个受支持类型经一对入口直接与 `std::string` 互转(spec §16)。

```cpp
template <typename T> std::string to_string(const T& v);
template <typename T> error_code from_string(string_view s, T& out);
template <typename T> std::optional<T> from_string(string_view s);
```

### 5.1 序列化形式

| 类型 | `to_string` 形式 | `from_string` 接受 |
| --- | --- | --- |
| `bool` | `"true"` / `"false"` | `"true"`/`"false"`,否则 `kind_mismatch` |
| 整型 | 十进制数字(`std::to_string`) | 整串解析;范围外 `overflow`;畸形文本 `invalid_number` |
| float / double | 经 `std::to_chars` 的最短往返文本,无点/指数时补 `".0"`,确定性 `"inf"`/`"-inf"`/`"nan"` | `from_chars` 直读;`inf`/`-inf`/`infinity`/`-infinity`/`nan` 令牌;`overflow`/`invalid_number` |
| `std::string` / `string_view` / char `basic_string` | 原始字节,不加引号 | 复制原始字节 |
| `char[N]` | 至首个 `'\0'` | 过长的输入 → `kind_mismatch`(绝不越界);缓冲清零后复制 |
| 已注册枚举 | 注册标签 | 标签匹配 → 值;否则 `kind_mismatch` |
| `date_time` | 规范 ISO 8601 UTC(亚秒精确往返) | ISO 8601 或数字 Unix 纪元(10/13/16/19/20 位推断 秒/毫秒/微秒/纳秒,spec §14.1) |
| 未注册枚举 | `""`(不支持类型) | `kind_mismatch` |
| 容器 / 反射结构体 / `optional` / `variant` / `tuple` / `pair` / `std::array` / `vector<bool>` / 原生数组 | 紧凑单行 JSON 字符串(见 §5.3) | 解析 JSON 后运行 `from_value` |

`to_string` 在转换本身失败时返回**空字符串**(如枚举值无标签)。标量
`from_string` 快路径对目标范围外的值报 `overflow`,畸形文本报 `invalid_number`;
kind 不匹配报 `kind_mismatch`。

### 5.2 架构:自持 `text_node` 树(spec §16.2)

容器/反射往返经最小的头内 `value_io` 树 — `rtti::detail::text_node` — 加紧凑
单行 JSON 序列化器(`serialize_text`)与紧凑 JSON 解析器(`parse_text`)复用既有
`to_value` / `from_value` 派发链。树自持(标量按值存 `std::string`;子节点经
`unique_ptr` 持有),故整个往返免费继承每个既有转换器(`optional`、`variant`、
`tuple`、`pair`、`array`、`vector<bool>`、queue/stack/priority_queue 适配器、
原生/多维 C 数组、ADL 钩子)。序列化确定:对象键序为源序,从不排序。容器内
非有限实数为裸令牌 `"inf"`/`"-inf"`/`"nan"`(字符串层自有解析器,精确往返
优先于 JSON 适配器采用的 `null` 映射)。

树与解析器在 `src/text_node.cpp`(仅编译一次),故头保持轻量:只有类声明与入口
点位于 `to_string.hpp:87-133`。`from_string` 错误映射:解析器码(`invalid_root`
/ `unexpected_token` / `unterminated_string` / `invalid_number` / `invalid_utf8`
/ `too_deep`)对应畸形文本;`kind_mismatch` / `missing_key` 对应转换类型不匹配;
`overflow` 对应数值范围违例。

### 5.3 任意进制整数桥(spec §15.4 / §16.1)

```cpp
std::string      to_string_base(std::uint64_t value, unsigned base, const char* digits = nullptr);
error_code       from_string_base(string_view s, unsigned base, std::uint64_t& out);
std::optional<std::uint64_t> from_string_base(string_view s, unsigned base);
std::string      to_string_base_signed(std::int64_t value, unsigned base);
error_code       from_string_base_signed(string_view s, unsigned base, std::int64_t& out);
std::optional<std::int64_t> from_string_base_signed(string_view s, unsigned base);
```

`rtti::detail::to_base`/`from_base`(进制 2..64)的薄包装,可选自定义数字表;有
符号变体带前导 `-`。无效进制/非法数字/溢出与底层 `base_convert.hpp` API 报告
完全一致(见 `utils.md` §2)。

```cpp
assert(rtti::to_string(42) == "42");
assert(rtti::to_string(3.5) == "3.5");
assert(rtti::to_string_base(255, 16) == "ff");
assert(rtti::from_string<int>("-7").value() == -7);
assert(rtti::from_string<std::vector<int>>("[1,2,3]").value() == std::vector<int>({1,2,3}));
```

---

## 6. `string_ops.hpp` — `rtti::str` 工具集

`rtti::str` 再导出统一转换层并追加纯字符串变换(spec §7.6 / Task 8a)。

### 6.1 转换别名

```cpp
using rtti::from_string;   // == rtti::from_string(同一实体,using-声明)
using rtti::to_string;     // == rtti::to_string

std::string          to_base(std::uint64_t value, unsigned base, const char* digits = nullptr);
std::optional<std::uint64_t> from_base(string_view s, unsigned base);
error_code           from_base(string_view s, unsigned base, std::uint64_t& out) noexcept;
std::string          to_base_signed(std::int64_t value, unsigned base);
std::optional<std::int64_t> from_base_signed(string_view s, unsigned base);
error_code           from_base_signed(string_view s, unsigned base, std::int64_t& out) noexcept;
```

`rtti::str::to_string` **即** `rtti::to_string`(同一实体,using-声明引入):整个
受支持类型面 — 标量、已注册枚举、`date_time`、容器、`optional`、`variant`、
`tuple`、`pair`、`std::array`、`vector<bool>`、原生数组、反射结构体 — 零漂移
转换。`from_base` 族原样桥接既有任意进制助手(进制 2..64)。

### 6.2 字符串操作

```cpp
std::vector<std::string> split(string_view s, char sep) noexcept;
std::vector<std::string> split(string_view s, string_view sep) noexcept;
std::string join(const std::vector<std::string>& parts, string_view sep) noexcept;

std::string trim(string_view s, string_view chars = " \t\r\n") noexcept;
std::string ltrim(string_view s, string_view chars = " \t\r\n") noexcept;
std::string rtrim(string_view s, string_view chars = " \t\r\n") noexcept;

std::string to_lower(string_view s) noexcept;   // ASCII 大小写折叠,逐字节
std::string to_upper(string_view s) noexcept;

bool starts_with(string_view s, string_view prefix) noexcept;  // 空前缀为 true
bool ends_with(string_view s, string_view suffix) noexcept;    // 空后缀为 true

std::string replace(string_view s, string_view from, string_view to) noexcept;
std::string replace_all(string_view s, string_view from, string_view to) noexcept;
std::string repeat(string_view s, std::size_t count) noexcept;

std::string pad_left(string_view s, std::size_t width, char fill = ' ') noexcept;
std::string pad_right(string_view s, std::size_t width, char fill = ' ') noexcept;

std::string reverse(string_view s) noexcept;    // 字节序

std::string quote(string_view s) noexcept;
std::string unquote(string_view s) noexcept;
error_code  unquote(string_view s, std::string& out) noexcept;

std::string escape(string_view s) noexcept;
std::string unescape(string_view s) noexcept;
error_code  unescape(string_view s, std::string& out) noexcept;
```

语义(纯变换均 `noexcept`;解析以 `error_code` 报告,永不崩溃 — spec §0):

- `split` — 每个 `sep` 出现处切分词元。空输入 → 空 vector;前导/尾随分隔符
  产生空边词元(`split("a,b,,c", ',') == {"a","b","","c"}`)。空字符串分隔符把
  整个输入当一个词元。
- `join` — 以 `sep` 连接词元;空词元表连接为 `""`。
- `trim` / `ltrim` / `rtrim` — 剥除 `chars` 的前导/尾随出现(默认空格、制表、
  CR、LF)。空 `chars` 保持输入不变。
- `to_lower` / `to_upper` — ASCII 大小写折叠,逐字节;非 ASCII 字节原样通过。
- `starts_with` / `ends_with` — 空前缀/空后缀均返回 `true`。
- `replace` — `from` 的**首个**出现;`replace_all` — 每个不重叠出现。空
  `from` 原样返回输入(绝不会死循环);空 `to` 删除匹配文本。
- `repeat` — `s` 重复 `count` 次;`count == 0` 得 `""`(不可表示的过大次数
  失败即安全为 `""`)。
- `pad_left` / `pad_right` — 以 `fill` 填充至 `width`;达到或超过 `width` 的
  输入原样返回。
- `quote` — 以双引号包裹 `s` 并转义内部 JSON/C 风格,故 `unquote(quote(s)) == s`
  恒往返。
- `unquote` — 输入不以 `"` 开头为 `kind_mismatch`;不以 `"` 结尾为
  `unterminated_string`;内部转义错误自 `unescape` 传播。`std::string` 重载任何
  解析失败返回 `""`。
- `escape` — 编码 `"`、`\`、`\b`、`\f`、`\n`、`\r`、`\t` 及每个低于 `0x20`
  的控制字节为 `\u00XX`。
- `unescape` — 解码上述转义;`\uXXXX` 接受四枚十六进制数字,紧随高代理项的
  `\uXXXX` 低代理项合并为单个码点。尾随孤立反斜杠 → `unterminated_string`;
  未知转义 → `unexpected_token`;畸形 `\uXXXX`(含未配对代理项) → `invalid_utf8`。

```cpp
using rtti::str::split;
using rtti::str::to_string;
assert(to_string(255) == "255");          // 经别名即 rtti::to_string
assert(split("a,b,,c", ',') == (std::vector<std::string>{"a","b","","c"}));
assert(rtti::str::trim("  hi \t\n") == "hi");
assert(rtti::str::replace_all("aXbXc", "X", "--") == "a--b--c");
assert(rtti::str::unquote(rtti::str::quote("a\"b")) == "a\"b");          // 往返
assert(rtti::str::escape("a\nb") == "a\\nb");
```

---

## 7. 相关文档

- 反射核心:[`reflection.md`](reflection.md) — 注册宏、`fields_of`、
  `field_value`、`value_io`、`field_access_base`、`type_desc`、
  `object_ref` / `instance`、`ctor_arg`/`ctor_registry`。
- 值转换:[`convert.md`](convert.md) — `converter<T>`、`from_value`/`to_value`、
  命名转换、数字/节点格式化、`memory`。
- 工具与支持:[`utils.md`](utils.md) — `base_convert`、`date_time`、`naming`、
  `version`、`rtti_fwd`。
- 设计与架构:[`../../design/zh/architecture.md`](../../design/zh/architecture.md)。
- English edition (英文版): [`en/reflect-tools.md`](../en/reflect-tools.md)。