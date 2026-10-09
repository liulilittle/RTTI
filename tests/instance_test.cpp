// Reflective instantiation + full operations tests. Task M of the RTTI
// perf-usability plan (spec section 18). Covers make_object<T> (no-arg and
// runtime parameter injection by position/name/mixed), argument injection
// errors (error_code, never crash), the instance handle's full operations
// (fields, iteration, containers, attributes, serialization, type query), the
// placement-new allocator path with manual destroy, the ORM CreateInstance /
// Bind contracts and the type-erased heterogeneous instance handle. Core-only:
// no format adapter headers (value_io behavior comes from the box test double).

#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#include "box.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace rtti_test_detail {

// Simple 2D point with a two-int constructor.
struct InstPoint {
    int x = 0;
    int y = 0;
    InstPoint() = default;
    InstPoint(int xv, int yv) : x(xv), y(yv) {}
};

// Person with a string+int constructor and an extra bool field.
struct InstPerson {
    std::string name;
    int age = 0;
    bool active = false;
    InstPerson() = default;
    InstPerson(std::string n, int a) : name(std::move(n)), age(a) {}
};

// Optional second parameter: omitted args use the registered default.
struct InstRanged {
    int lo = 0;
    int hi = 0;
    InstRanged() = default;
    InstRanged(int l, int h) : lo(l), hi(h) {}
};

// Container (value_io node) parameter injection.
struct InstNamed {
    std::vector<int> tags;
    std::string label;
    InstNamed() = default;
    InstNamed(std::vector<int> t, std::string l) : tags(std::move(t)), label(std::move(l)) {}
};

// RAII lifetime counter.
struct InstCounted {
    static int live;
    int v = 0;
    InstCounted() { ++live; }
    InstCounted(int vv) : v(vv) { ++live; }
    ~InstCounted() { --live; }
};
int InstCounted::live = 0;

// Placement lifetime counter.
struct InstPlacement {
    static int constructed;
    static int destroyed;
    int v = 0;
    InstPlacement() { ++constructed; }
    InstPlacement(int vv) : v(vv) { ++constructed; }
    ~InstPlacement() { ++destroyed; }
};
int InstPlacement::constructed = 0;
int InstPlacement::destroyed = 0;

// Reflected-field-only type: never registered for a ctor.
struct InstPlain {
    int v = 0;
};

// Bool parameter lane: ctor injection accepts integer 0/1 for bool params.
struct InstBoolCtor {
    bool flag = false;
    int v = 0;
    InstBoolCtor() = default;
    InstBoolCtor(bool f, int vv) : flag(f), v(vv) {}
};

} // namespace rtti_test_detail

// Field tables (non-intrusive macro family; the types have public members).
RTTI_DEFINE_TYPE(rtti_test_detail::InstPoint, x, y)
RTTI_DEFINE_TYPE(rtti_test_detail::InstPerson, name, age, active)
RTTI_DEFINE_TYPE(rtti_test_detail::InstRanged, lo, hi)
RTTI_DEFINE_TYPE(rtti_test_detail::InstNamed, tags, label)
RTTI_DEFINE_TYPE(rtti_test_detail::InstCounted, v)
RTTI_DEFINE_TYPE(rtti_test_detail::InstPlacement, v)
RTTI_DEFINE_TYPE(rtti_test_detail::InstPlain, v)
RTTI_DEFINE_TYPE(rtti_test_detail::InstBoolCtor, flag, v)

// Constructor registries. The optional flag marks injectable-optional params.
RTTI_DEFINE_CTOR(rtti_test_detail::InstPoint, ("x", int, false), ("y", int, false))
RTTI_DEFINE_CTOR(rtti_test_detail::InstPerson, ("name", std::string, false), ("age", int, false))
RTTI_DEFINE_CTOR(rtti_test_detail::InstRanged, ("lo", int, false), ("hi", int, true))
RTTI_DEFINE_CTOR(rtti_test_detail::InstNamed, ("tags", std::vector<int>, false), ("label", std::string, false))
RTTI_DEFINE_CTOR(rtti_test_detail::InstCounted, ("v", int, false))
RTTI_DEFINE_CTOR(rtti_test_detail::InstPlacement, ("v", int, false))
RTTI_DEFINE_CTOR(rtti_test_detail::InstBoolCtor, ("flag", bool, false), ("v", int, false))

// Attributes for the full-ops attribute assertions (section 17 coexistence).
RTTI_DEFINE_TYPE_ATTRS(rtti_test_detail::InstPoint,
    RTTI_ATTR("Serializable"),
    RTTI_ATTR("TableName", "points"))
RTTI_DEFINE_FIELD_ATTRS(rtti_test_detail::InstPoint,
    RTTI_FIELD_ATTR_ENTRY(x, RTTI_ATTR("Key")),
    RTTI_FIELD_ATTR_ENTRY(y, RTTI_ATTR("ColumnName", "pos_y")))

namespace rtti_test_lib {

// Counting allocator for the placement tests: tracks outstanding allocations
// and the free count, so construction/destruction and the manual contract are
// verifiable without leaks. `total_frees` counts every deallocate in the
// process so R2-3's release snapshot is still observable on the caller's copy
// (the caller's own `frees` stays 0 once release runs through the snapshot).
struct InstCountedAlloc {
    static int total_frees;
    int allocs = 0;
    int frees = 0;
    void* alloc(std::size_t size, std::size_t align) {
        (void)align;
        ++allocs;
        return ::operator new(size);
    }
    void deallocate(void* p, std::size_t /*size*/, std::size_t /*align*/) {
        ++frees;
        ++total_frees;
        ::operator delete(p);
    }
};
int InstCountedAlloc::total_frees = 0;

// R2-3: an allocator whose instance POISONS itself on destruction. The
// placement release path must snapshot the allocator (capture by value), so a
// handle that outlives the caller's allocator object can still return the
// memory without touching the dead (poisoned) instance.
struct InstDoomedAlloc {
    static int use_after_destroy;
    static int frees_total;
    int allocs = 0;
    bool dead = false;

    InstDoomedAlloc() noexcept = default;
    InstDoomedAlloc(const InstDoomedAlloc& other) noexcept
        : allocs(other.allocs), dead(other.dead) {}
    ~InstDoomedAlloc() { dead = true; } // any later deallocate is a UAF probe

    void* alloc(std::size_t size, std::size_t /*align*/) {
        ++allocs;
        return ::operator new(size);
    }
    void deallocate(void* p, std::size_t /*size*/, std::size_t /*align*/) {
        if (dead) {
            ++use_after_destroy; // release reached the destroyed allocator
        }
        ++frees_total;
        ::operator delete(p);
    }
};
int InstDoomedAlloc::use_after_destroy = 0;
int InstDoomedAlloc::frees_total = 0;

RTTI_TEST_CASE(InstTestDefaultConstruct) {
    auto h = rtti::make_object<rtti_test_detail::InstPoint>();
    RTTI_CHECK(h.valid());
    RTTI_CHECK_EQ(h->x, 0);
    RTTI_CHECK_EQ(h->y, 0);
    RTTI_CHECK(h.type_of() != nullptr);
}

RTTI_TEST_CASE(InstTestPositionalArgs) {
    auto r = rtti::make_object<rtti_test_detail::InstPoint>({
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(3))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(4))),
    });
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value->x, 3);
    RTTI_CHECK_EQ(r.value->y, 4);
}

RTTI_TEST_CASE(InstTestNamedArgsOutOfOrder) {
    auto r = rtti::make_object<rtti_test_detail::InstPoint>({
        rtti::ctor_arg::scalar("y", rtti::field_value::of(int64_t(5))),
        rtti::ctor_arg::scalar("x", rtti::field_value::of(int64_t(7))),
    });
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value->x, 7);
    RTTI_CHECK_EQ(r.value->y, 5);
}

RTTI_TEST_CASE(InstTestMixedArgs) {
    auto r = rtti::make_object<rtti_test_detail::InstPerson>({
        rtti::ctor_arg::scalar("name", rtti::field_value::of(rtti::string_view("zoe"))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(28))),
    });
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value->name, std::string("zoe"));
    RTTI_CHECK_EQ(r.value->age, 28);
}

RTTI_TEST_CASE(InstTestMissingRequiredParam) {
    auto r = rtti::make_object<rtti_test_detail::InstPerson>({
        rtti::ctor_arg::scalar(rtti::field_value::of(rtti::string_view("solo"))),
    });
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::missing_key);
    // No instance leaked on failure.
    RTTI_CHECK(!r.value.valid());
}

RTTI_TEST_CASE(InstTestWrongType) {
    auto r = rtti::make_object<rtti_test_detail::InstPerson>({
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(1))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(2))),
    });
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(InstTestOptionalParamDefault) {
    // Optional "hi" omitted: filled with a value-initialized default (0). The
    // runtime injection cannot see the C++ default argument; a registered
    // ctor_param::def would override.
    auto r = rtti::make_object<rtti_test_detail::InstRanged>({
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(5))),
    });
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value->lo, 5);
    RTTI_CHECK_EQ(r.value->hi, 0);
    // Full positional still works.
    auto r2 = rtti::make_object<rtti_test_detail::InstRanged>({
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(1))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(9))),
    });
    RTTI_CHECK(r2.ok());
    RTTI_CHECK_EQ(r2.value->hi, 9);
}

RTTI_TEST_CASE(InstTestUnknownName) {
    auto r = rtti::make_object<rtti_test_detail::InstPoint>({
        rtti::ctor_arg::scalar("zz", rtti::field_value::of(int64_t(1))),
        rtti::ctor_arg::scalar("x", rtti::field_value::of(int64_t(2))),
    });
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(InstTestTooManyPositional) {
    auto r = rtti::make_object<rtti_test_detail::InstPoint>({
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(1))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(2))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(3))),
    });
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(InstTestUnregisteredCtorError) {
    auto r = rtti::make_object<rtti_test_detail::InstPlain>({
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(1))),
    });
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::kind_mismatch);
    // Default construction of an unregistered type is still allowed.
    auto h = rtti::make_object<rtti_test_detail::InstPlain>();
    RTTI_CHECK(h.valid());
    RTTI_CHECK_EQ(h->v, 0);
}

RTTI_TEST_CASE(InstTestBoolParamLenient) {
    // Ctor injection accepts integer 0/1 for a bool parameter.
    auto r = rtti::make_object<rtti_test_detail::InstBoolCtor>({
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(1))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(5))),
    });
    RTTI_CHECK(r.ok());
    RTTI_CHECK(r.value->flag);
    RTTI_CHECK_EQ(r.value->v, 5);
    auto r2 = rtti::make_object<rtti_test_detail::InstBoolCtor>({
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(0))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(6))),
    });
    RTTI_CHECK(r2.ok());
    RTTI_CHECK(!r2.value->flag);
}

RTTI_TEST_CASE(InstTestValueIoNodeArg) {
    auto* arr = rtti_test_lib::make_arr();
    arr->push_back(rtti_test_lib::make_int(5));
    arr->push_back(rtti_test_lib::make_int(6));
    auto r = rtti::make_object<rtti_test_detail::InstNamed>({
        rtti::ctor_arg::node(arr),
        rtti::ctor_arg::scalar(rtti::field_value::of(rtti::string_view("lab"))),
    });
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value->tags.size(), size_t(2));
    RTTI_CHECK_EQ(r.value->tags[0], 5);
    RTTI_CHECK_EQ(r.value->tags[1], 6);
    RTTI_CHECK_EQ(r.value->label, std::string("lab"));
    delete arr;
}

RTTI_TEST_CASE(InstTestFullOpsFields) {
    auto r = rtti::make_object<rtti_test_detail::InstPerson>({
        rtti::ctor_arg::scalar(rtti::field_value::of(rtti::string_view("ada"))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(36))),
    });
    RTTI_CHECK(r.ok());
    rtti::object_ref<rtti_test_detail::InstPerson>& h = r.value;
    rtti::field_value fv;
    RTTI_CHECK_EQ(h.get("name", fv), rtti::error_code::ok);
    RTTI_CHECK_EQ(std::string(fv.as_string()), std::string("ada"));
    RTTI_CHECK_EQ(h.get("age", fv), rtti::error_code::ok);
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(36));
    RTTI_CHECK_EQ(h.get("missing", fv), rtti::error_code::no_such_field);

    RTTI_CHECK_EQ(h.set("age", rtti::field_value::of(int64_t(37))), rtti::error_code::ok);
    RTTI_CHECK_EQ(h->age, 37);
    RTTI_CHECK_EQ(h.set("missing", rtti::field_value::of(int64_t(0))), rtti::error_code::no_such_field);
    RTTI_CHECK_EQ(h.set("age", rtti::field_value::of(rtti::string_view("x"))), rtti::error_code::kind_mismatch);
    // Field set mirrors the core field_value_to lanes (strict bool).
    RTTI_CHECK_EQ(h.set("active", rtti::field_value::of(true)), rtti::error_code::ok);
    RTTI_CHECK(h->active);
}

RTTI_TEST_CASE(InstTestFullOpsFieldIteration) {
    auto r = rtti::make_object<rtti_test_detail::InstPerson>({
        rtti::ctor_arg::scalar(rtti::field_value::of(rtti::string_view("ada"))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(36))),
    });
    RTTI_CHECK(r.ok());
    std::size_t n = 0;
    std::string names;
    for (auto it = rtti::field_begin<rtti_test_detail::InstPerson>();
         it != rtti::field_end<rtti_test_detail::InstPerson>(); ++it) {
        ++n;
        const rtti::field_access_base* fa = *it;
        RTTI_CHECK(fa != nullptr);
        names += fa->name;
    }
    RTTI_CHECK_EQ(n, size_t(3));
    RTTI_CHECK_EQ(names, std::string("nameageactive"));
}

RTTI_TEST_CASE(InstTestFullOpsContainers) {
    auto* arr = rtti_test_lib::make_arr();
    auto r = rtti::make_object<rtti_test_detail::InstNamed>({
        rtti::ctor_arg::node(arr),
        rtti::ctor_arg::scalar(rtti::field_value::of(rtti::string_view("n"))),
    });
    delete arr;
    RTTI_CHECK(r.ok());
    rtti::object_ref<rtti_test_detail::InstNamed>& h = r.value;

    RTTI_CHECK_EQ(h.field_size("tags"), size_t(0));
    RTTI_CHECK_EQ(h.add("tags", rtti::field_value::of(int64_t(10))), rtti::error_code::ok);
    RTTI_CHECK_EQ(h.add("tags", rtti::field_value::of(int64_t(20))), rtti::error_code::ok);
    RTTI_CHECK_EQ(h.field_size("tags"), size_t(2));
    RTTI_CHECK_EQ(h.erase_at("tags", size_t(0)), rtti::error_code::ok);
    RTTI_CHECK_EQ(h.field_size("tags"), size_t(1));
    RTTI_CHECK_EQ(h.erase_at("tags", size_t(9)), rtti::error_code::no_such_field);
    RTTI_CHECK_EQ(h.clear("tags"), rtti::error_code::ok);
    RTTI_CHECK_EQ(h.field_size("tags"), size_t(0));
    RTTI_CHECK_EQ(h.add("nope", rtti::field_value::of(int64_t(1))), rtti::error_code::no_such_field);

    rtti::error_code ec = rtti::error_code::ok;
    RTTI_CHECK_EQ(h.field_size("label", &ec), size_t(0));
    RTTI_CHECK(ec == rtti::error_code::kind_mismatch);
}

RTTI_TEST_CASE(InstTestFullOpsForEachElement) {
    auto* arr = rtti_test_lib::make_arr();
    auto r = rtti::make_object<rtti_test_detail::InstNamed>({
        rtti::ctor_arg::node(arr),
        rtti::ctor_arg::scalar(rtti::field_value::of(rtti::string_view("n"))),
    });
    delete arr;
    RTTI_CHECK(r.ok());
    rtti::object_ref<rtti_test_detail::InstNamed>& h = r.value;
    RTTI_CHECK_EQ(h.add("tags", rtti::field_value::of(int64_t(3))), rtti::error_code::ok);
    RTTI_CHECK_EQ(h.add("tags", rtti::field_value::of(int64_t(4))), rtti::error_code::ok);

    long long sum = 0;
    rtti_test_lib::box scratch;
    const auto ec = h.for_each_element(
        "tags", &scratch,
        [&sum](std::size_t, rtti::string_view, const rtti::value_io* e) -> bool {
            int64_t v = 0;
            RTTI_CHECK(e->as_integer(v));
            sum += v;
            return true;
        });
    RTTI_CHECK_EQ(ec, rtti::error_code::ok);
    RTTI_CHECK_EQ(sum, static_cast<long long>(7));

    // E-section element_iterator works on the raw instance through the handle.
    rtti::element_iterator<rtti_test_detail::InstNamed> it;
    RTTI_CHECK_EQ(rtti::element_begin(*h, "tags", it), rtti::error_code::ok);
    RTTI_CHECK(!it.at_end());
    RTTI_CHECK_EQ(it.element_value().as_integer(), int64_t(3));
    ++it;
    RTTI_CHECK_EQ(it.element_value().as_integer(), int64_t(4));
    ++it;
    RTTI_CHECK(it.at_end());
}

RTTI_TEST_CASE(InstTestFullOpsAttrs) {
    auto r = rtti::make_object<rtti_test_detail::InstPoint>({
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(1))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(2))),
    });
    RTTI_CHECK(r.ok());
    rtti::object_ref<rtti_test_detail::InstPoint>& h = r.value;
    RTTI_CHECK_EQ(h.attr_count(), size_t(2));
    const rtti::attr* t = h.find_attr("TableName");
    RTTI_CHECK(t != nullptr);
    RTTI_CHECK_EQ(std::string(t->name), std::string("TableName"));
    RTTI_CHECK_EQ(t->args.size(), size_t(1));
    RTTI_CHECK_EQ(t->args[0].as_string(), rtti::string_view("points"));
    RTTI_CHECK(h.has_attr("Serializable"));
    RTTI_CHECK(!h.has_attr("Nope"));
    const rtti::field_attr_list l = h.field_attr("x");
    RTTI_CHECK_EQ(l.count, size_t(1));
    RTTI_CHECK_EQ(std::string(l.items[0].name), std::string("Key"));
    RTTI_CHECK(h.field_has_attr("x", "Key"));
    RTTI_CHECK(!h.field_has_attr("y", "Key"));
    RTTI_CHECK_EQ(h.field_attr("y").count, size_t(1));
}

RTTI_TEST_CASE(InstTestFullOpsSerializeRoundtrip) {
    auto r = rtti::make_object<rtti_test_detail::InstPerson>({
        rtti::ctor_arg::scalar(rtti::field_value::of(rtti::string_view("neo"))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(33))),
    });
    RTTI_CHECK(r.ok());
    rtti::object_ref<rtti_test_detail::InstPerson>& h = r.value;
    RTTI_CHECK_EQ(h.set("active", rtti::field_value::of(true)), rtti::error_code::ok);

    // Serialize the instance into a value_io sink (the four format adapters
    // consume this same bridge).
    rtti_test_lib::box root;
    RTTI_CHECK_EQ(h.to_value(&root), rtti::error_code::ok);
    RTTI_CHECK(root.type() == rtti::value_io::kind::object);
    rtti::value_io* nv = nullptr;
    RTTI_CHECK(root.get_key("name", nv));
    RTTI_CHECK(nv != nullptr && nv->type() == rtti::value_io::kind::string);

    // Deserialize from a value_io source, overwriting the fields.
    rtti_test_lib::box src;
    src.make_object();
    src.set_key("name", rtti_test_lib::make_str("trinity"));
    src.set_key("age", rtti_test_lib::make_int(28));
    auto* bv = new rtti_test_lib::box;
    bv->make_scalar(rtti::field_value::of(true));
    src.set_key("active", bv);
    RTTI_CHECK_EQ(h.from_value(&src), rtti::error_code::ok);
    RTTI_CHECK_EQ(h->name, std::string("trinity"));
    RTTI_CHECK_EQ(h->age, 28);
    RTTI_CHECK(h->active);
}

RTTI_TEST_CASE(InstTestTypeQuery) {
    auto h = rtti::make_object<rtti_test_detail::InstPoint>();
    RTTI_CHECK(h.valid());
    const rtti::type_desc* td = h.type_of();
    RTTI_CHECK(td != nullptr);
    RTTI_CHECK(td->kind == rtti::type_kind::reflected);
    RTTI_CHECK(td->as_reflected != nullptr);
    RTTI_CHECK_EQ(td->as_reflected->field_count, size_t(2));

    auto* arr = rtti_test_lib::make_arr();
    auto r = rtti::make_object<rtti_test_detail::InstNamed>({
        rtti::ctor_arg::node(arr),
        rtti::ctor_arg::scalar(rtti::field_value::of(rtti::string_view("n"))),
    });
    delete arr;
    RTTI_CHECK(r.ok());
    const rtti::type_desc* et = r.value.element_type("tags");
    RTTI_CHECK(et != nullptr);
    RTTI_CHECK(et->kind == rtti::type_kind::integer);
    RTTI_CHECK(r.value.element_type("label") == nullptr);
    RTTI_CHECK(r.value.element_type("nope") == nullptr);
}

RTTI_TEST_CASE(InstTestRaiiLifetime) {
    RTTI_CHECK_EQ(rtti_test_detail::InstCounted::live, 0);
    {
        auto h = rtti::make_object<rtti_test_detail::InstCounted>({
            rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(7))),
        });
        RTTI_CHECK(h.ok());
        RTTI_CHECK_EQ(h.value->v, 7);
        RTTI_CHECK_EQ(rtti_test_detail::InstCounted::live, 1);
    }
    RTTI_CHECK_EQ(rtti_test_detail::InstCounted::live, 0);
    {
        auto h = rtti::make_object<rtti_test_detail::InstCounted>();
        RTTI_CHECK(h.valid());
        RTTI_CHECK_EQ(rtti_test_detail::InstCounted::live, 1);
    }
    RTTI_CHECK_EQ(rtti_test_detail::InstCounted::live, 0);
}

RTTI_TEST_CASE(InstTestPlacementManualLifetime) {
    RTTI_CHECK_EQ(rtti_test_detail::InstPlacement::constructed,
                  rtti_test_detail::InstPlacement::destroyed);
    InstCountedAlloc alloc;
    {
        auto h = rtti::make_object<rtti_test_detail::InstPlacement>(
            alloc, {rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(9)))});
        RTTI_CHECK(h.valid());
        RTTI_CHECK_EQ(h->v, 9);
        RTTI_CHECK_EQ(rtti_test_detail::InstPlacement::constructed,
                      rtti_test_detail::InstPlacement::destroyed + 1);
        RTTI_CHECK_EQ(alloc.allocs, 1);
        RTTI_CHECK_EQ(alloc.frees, 0);

        // Manual destroy: destructor + memory return. The release runs
        // through the handle's snapshot (R2-3), so the caller's `frees`
        // counter is untouched; the deallocate still happened exactly once.
        const int frees_before = InstCountedAlloc::total_frees;
        h.destroy();
        RTTI_CHECK(!h.valid());
        RTTI_CHECK_EQ(rtti_test_detail::InstPlacement::constructed,
                      rtti_test_detail::InstPlacement::destroyed);
        RTTI_CHECK_EQ(InstCountedAlloc::total_frees, frees_before + 1);

        // destroy() is idempotent (no double free).
        h.destroy();
        RTTI_CHECK_EQ(InstCountedAlloc::total_frees, frees_before + 1);
    }
    // Handle destruction in manual mode leaves allocator memory untouched.
    RTTI_CHECK_EQ(rtti_test_detail::InstPlacement::constructed,
                  rtti_test_detail::InstPlacement::destroyed);
}

RTTI_TEST_CASE(InstTestPlacementIntoArena) {
    rtti::memory_arena arena;
    RTTI_CHECK_EQ(rtti_test_detail::InstPlacement::constructed,
                  rtti_test_detail::InstPlacement::destroyed);
    {
        auto h = rtti::make_object<rtti_test_detail::InstPlacement>(
            arena, {rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(3)))});
        RTTI_CHECK(h.valid());
        RTTI_CHECK_EQ(h->v, 3);
        RTTI_CHECK_EQ(rtti_test_detail::InstPlacement::constructed,
                      rtti_test_detail::InstPlacement::destroyed + 1);
        // Arena has no per-object release: destroy() only runs the destructor.
        h.destroy();
        RTTI_CHECK(!h.valid());
        RTTI_CHECK_EQ(rtti_test_detail::InstPlacement::constructed,
                      rtti_test_detail::InstPlacement::destroyed);
    }
    RTTI_CHECK_EQ(rtti_test_detail::InstPlacement::constructed,
                  rtti_test_detail::InstPlacement::destroyed);
}

RTTI_TEST_CASE(InstTestPlacementBadArgsNoAlloc) {
    InstCountedAlloc alloc;
    // "name" expects a string; an integer scalar is a kind mismatch, so no
    // memory is taken from the allocator and no instance is constructed.
    auto h = rtti::make_object<rtti_test_detail::InstPerson>(
        alloc, {rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(1)))});
    RTTI_CHECK(!h.valid());
    RTTI_CHECK_EQ(alloc.allocs, 0);
    RTTI_CHECK_EQ(alloc.frees, 0);
}

// R2-3: the placement release lambda must not capture the caller's allocator
// by reference. Here the allocator is destroyed BEFORE the handle; the later
// destroy() must return the memory through the handle's own snapshot without
// touching the destroyed allocator (a by-reference capture would call
// deallocate on the poisoned instance and trip the probe).
RTTI_TEST_CASE(InstTestPlacementHandleOutlivesAllocator) {
    RTTI_CHECK_EQ(rtti_test_lib::InstDoomedAlloc::use_after_destroy, 0);
    RTTI_CHECK_EQ(rtti_test_lib::InstDoomedAlloc::frees_total, 0);
    rtti::object_ref<rtti_test_detail::InstPlacement> h;
    {
        rtti_test_lib::InstDoomedAlloc alloc;
        auto mh = rtti::make_object<rtti_test_detail::InstPlacement>(
            alloc, {rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(9)))});
        RTTI_CHECK(mh.valid());
        RTTI_CHECK_EQ(mh->v, 9);
        RTTI_CHECK_EQ(alloc.allocs, 1);
        h = std::move(mh);
    } // alloc destroyed here: any later deallocate on it is use-after-destroy

    RTTI_CHECK(h.valid());
    h.destroy();
    RTTI_CHECK(!h.valid());
    // The release ran through the handle's snapshot: no destroy() work
    // touched the dead allocator, and the memory was still returned.
    RTTI_CHECK_EQ(rtti_test_lib::InstDoomedAlloc::use_after_destroy, 0);
    RTTI_CHECK_EQ(rtti_test_lib::InstDoomedAlloc::frees_total, 1);
    RTTI_CHECK_EQ(rtti_test_detail::InstPlacement::constructed,
                  rtti_test_detail::InstPlacement::destroyed);
}

RTTI_TEST_CASE(InstTestOrmContract) {
    // CreateInstance(ctor_args) with named injection (column -> parameter).
    auto h = rtti::create_instance<rtti_test_detail::InstPerson>({
        rtti::ctor_arg::scalar("name", rtti::field_value::of(rtti::string_view("zoe"))),
        rtti::ctor_arg::scalar("age", rtti::field_value::of(int64_t(28))),
    });
    RTTI_CHECK(h.ok());
    RTTI_CHECK_EQ(h.value->name, std::string("zoe"));
    RTTI_CHECK_EQ(h.value->age, 28);

    // Bind<T>(row -> object): set reflected fields by name from a result row.
    rtti::row_cell row[] = {
        {"name", rtti::field_value::of(rtti::string_view("dana"))},
        {"age", rtti::field_value::of(int64_t(31))},
        {"active", rtti::field_value::of(true)},
    };
    RTTI_CHECK_EQ(rtti::bind(h.value, row, 3), rtti::error_code::ok);
    RTTI_CHECK_EQ(h.value->name, std::string("dana"));
    RTTI_CHECK_EQ(h.value->age, 31);
    RTTI_CHECK(h.value->active);

    // Unknown column: error, instance stays valid.
    rtti::row_cell bad[] = {{"nope", rtti::field_value::of(int64_t(1))}};
    RTTI_CHECK_EQ(rtti::bind(h.value, bad, 1), rtti::error_code::no_such_field);

    // initializer_list overload.
    RTTI_CHECK_EQ(rtti::bind(h.value, {
        {"name", rtti::field_value::of(rtti::string_view("eve"))},
        {"age", rtti::field_value::of(int64_t(35))},
    }), rtti::error_code::ok);
    RTTI_CHECK_EQ(h.value->name, std::string("eve"));
    RTTI_CHECK_EQ(h.value->age, 35);
}

RTTI_TEST_CASE(InstTestTypeErasedInstance) {
    auto r = rtti::make_object<rtti_test_detail::InstPoint>({
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(3))),
        rtti::ctor_arg::scalar(rtti::field_value::of(int64_t(4))),
    });
    RTTI_CHECK(r.ok());
    rtti::instance inst = rtti::instance::from(std::move(r.value));
    RTTI_CHECK(inst.valid());
    RTTI_CHECK(inst.type_of() != nullptr);
    RTTI_CHECK(inst.type_of()->kind == rtti::type_kind::reflected);

    rtti::field_value fv;
    RTTI_CHECK_EQ(inst.get("x", fv), rtti::error_code::ok);
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(3));
    RTTI_CHECK_EQ(inst.set("y", rtti::field_value::of(int64_t(9))), rtti::error_code::ok);

    rtti_test_lib::box out;
    RTTI_CHECK_EQ(inst.to_value(&out), rtti::error_code::ok);
    RTTI_CHECK(out.type() == rtti::value_io::kind::object);

    inst.destroy();
    RTTI_CHECK(!inst.valid());
}

} // namespace rtti_test_lib