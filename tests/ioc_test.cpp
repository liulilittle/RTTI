// IOC container / dependency-injection / resolution tests. Follow-up to task
// M (spec section 18): a thin IOC layer on top of the reflective
// instantiation. Covers register_type (singleton / transient / named),
// register_as (interface -> impl polymorphism), register_factory,
// resolve<T>/get<T> (object_ref<T> return, error_code safety for unregistered
// types), constructor injection (a ctor parameter whose type is a registered
// dependency is auto-injected; scalar parameters require optional or fail),
// cycle detection (error_code, never recurses forever), RAII lifetime (the
// container releases all singletons; transient handles are caller-owned) and
// the object_ref full-ops cooperation with task M (fields / iteration /
// type_of / serialization). Core-only: no format adapter headers (value_io
// behavior comes from the box test double). All names are prefixed IocTest*
// so they stay unique across translation units.

#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#include <rtti/ioc.hpp>
#include "box.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rtti_test_detail {

// Counting lifetime probe: construction/destruction balance is observable.
struct IocTestCounted {
    static int live;
    int v = 0;
    IocTestCounted() { ++live; }
    explicit IocTestCounted(int vv) : v(vv) { ++live; }
    IocTestCounted(const IocTestCounted& o) : v(o.v) { ++live; }
    IocTestCounted(IocTestCounted&& o) noexcept : v(o.v) { ++live; }
    IocTestCounted& operator=(const IocTestCounted& o) {
        v = o.v;
        return *this;
    }
    IocTestCounted& operator=(IocTestCounted&& o) noexcept {
        v = o.v;
        return *this;
    }
    ~IocTestCounted() { --live; }
};
int IocTestCounted::live = 0;

// Reflected struct used for register/resolve and the full-ops checks.
struct IocTestPointRefl {
    int x = 0;
    int y = 0;
    IocTestPointRefl() = default;
    void set(int a, int b) {
        x = a;
        y = b;
    }
};

// Constructor with two optional scalar parameters: resolve fills defaults.
struct IocTestRanged {
    int lo = 0;
    int hi = 0;
    IocTestRanged() = default;
    IocTestRanged(int l, int h) : lo(l), hi(h) {}
};

// A registered service (singleton dependency target).
struct IocTestService {
    std::string id;
    IocTestService() = default;
    explicit IocTestService(std::string s) : id(std::move(s)) {}
};

// Consumer whose ctor injects the service dependency by pointer.
struct IocTestConsumer {
    IocTestService* srv = nullptr;
    IocTestConsumer() = default;
    explicit IocTestConsumer(IocTestService* s) : srv(s) {}
};

// Consumer with an injected dependency plus an optional scalar parameter.
struct IocTestConsumer2 {
    IocTestService* srv = nullptr;
    int count = 0;
    IocTestConsumer2() = default;
    IocTestConsumer2(IocTestService* s, int c) : srv(s), count(c) {}
};

// Non-optional scalar ctor parameter: resolve must fail with missing_key.
struct IocTestNeedScalar {
    int n = 0;
    IocTestNeedScalar() = default;
    explicit IocTestNeedScalar(int v) : n(v) {}
};

// Mutual dependencies: resolve must detect the cycle and fail safely.
struct IocTestCycleA;
struct IocTestCycleB;
struct IocTestCycleA {
    IocTestCycleB* b = nullptr;
    IocTestCycleA() = default;
    explicit IocTestCycleA(IocTestCycleB* p) : b(p) {}
};
struct IocTestCycleB {
    IocTestCycleA* a = nullptr;
    IocTestCycleB() = default;
    explicit IocTestCycleB(IocTestCycleA* p) : a(p) {}
};

// Abstract interface for register_as polymorphism.
struct IocTestWidgetView {
    virtual ~IocTestWidgetView() = default;
    virtual int value() const = 0;
    virtual void bump() = 0;
};

// Concrete implementation registered as the widget view.
struct IocTestWidgetImpl : IocTestWidgetView {
    int n = 0;
    int extra = 0;
    IocTestWidgetImpl() = default;
    explicit IocTestWidgetImpl(int v) : n(v) {}
    int value() const override { return n; }
    void bump() override { ++n; }
};

// Factory-produced type.
struct IocTestMade {
    int tag = 0;
    IocTestMade() = default;
};

} // namespace rtti_test_detail

// Field tables (non-intrusive macro family; the types have public members).
// Cycle types and the widget interface need no reflection.
RTTI_DEFINE_TYPE(rtti_test_detail::IocTestCounted, v)
RTTI_DEFINE_TYPE(rtti_test_detail::IocTestPointRefl, x, y)
RTTI_DEFINE_TYPE(rtti_test_detail::IocTestRanged, lo, hi)
RTTI_DEFINE_TYPE(rtti_test_detail::IocTestService, id)
RTTI_DEFINE_TYPE(rtti_test_detail::IocTestWidgetImpl, n, extra)
RTTI_DEFINE_TYPE(rtti_test_detail::IocTestMade, tag)

// Constructor registries for the injection / optional-scalar paths.
RTTI_DEFINE_CTOR(rtti_test_detail::IocTestRanged, ("lo", int, true), ("hi", int, true))
RTTI_DEFINE_CTOR(rtti_test_detail::IocTestConsumer,
                 ("srv", rtti_test_detail::IocTestService*, false))
RTTI_DEFINE_CTOR(rtti_test_detail::IocTestConsumer2,
                 ("srv", rtti_test_detail::IocTestService*, false),
                 ("count", int, true))
RTTI_DEFINE_CTOR(rtti_test_detail::IocTestNeedScalar, ("n", int, false))
RTTI_DEFINE_CTOR(rtti_test_detail::IocTestCycleA,
                 ("b", rtti_test_detail::IocTestCycleB*, false))
RTTI_DEFINE_CTOR(rtti_test_detail::IocTestCycleB,
                 ("a", rtti_test_detail::IocTestCycleA*, false))

namespace rtti_test_lib {

using rtti::ioc::container;
using rtti::ioc::lifetime;

RTTI_TEST_CASE(IocTestSingletonSameInstance) {
    container c;
    c.register_type<rtti_test_detail::IocTestPointRefl>(lifetime::singleton);
    auto a = c.resolve<rtti_test_detail::IocTestPointRefl>();
    RTTI_CHECK(a.ok());
    RTTI_CHECK(a.value.valid());
    a.value->set(2, 3);
    auto b = c.resolve<rtti_test_detail::IocTestPointRefl>();
    RTTI_CHECK(b.ok());
    // Singleton: the same instance is reused.
    RTTI_CHECK(a.value.get() == b.value.get());
    RTTI_CHECK_EQ(b.value->x, 2);
}

RTTI_TEST_CASE(IocTestTransientNewInstanceEachResolve) {
    container c;
    c.register_type<rtti_test_detail::IocTestPointRefl>(lifetime::transient);
    auto a = c.resolve<rtti_test_detail::IocTestPointRefl>();
    auto b = c.resolve<rtti_test_detail::IocTestPointRefl>();
    RTTI_CHECK(a.ok());
    RTTI_CHECK(b.ok());
    RTTI_CHECK(a.value.get() != b.value.get());
}

RTTI_TEST_CASE(IocTestResolveDefaultsToSingleton) {
    // register_type<T>() without a lifetime argument defaults to singleton.
    container c;
    c.register_type<rtti_test_detail::IocTestPointRefl>();
    auto a = c.resolve<rtti_test_detail::IocTestPointRefl>();
    auto b = c.resolve<rtti_test_detail::IocTestPointRefl>();
    RTTI_CHECK(a.ok());
    RTTI_CHECK(b.ok());
    RTTI_CHECK(a.value.get() == b.value.get());
}

RTTI_TEST_CASE(IocTestNamedRegistration) {
    container c;
    c.register_type<rtti_test_detail::IocTestPointRefl>("alpha", lifetime::singleton);
    c.register_type<rtti_test_detail::IocTestPointRefl>("beta", lifetime::singleton);
    auto a = c.resolve<rtti_test_detail::IocTestPointRefl>("alpha");
    auto b = c.resolve<rtti_test_detail::IocTestPointRefl>("beta");
    RTTI_CHECK(a.ok());
    RTTI_CHECK(b.ok());
    RTTI_CHECK(a.value.get() != b.value.get());
    auto miss = c.resolve<rtti_test_detail::IocTestPointRefl>("gamma");
    RTTI_CHECK(!miss.ok());
    RTTI_CHECK(!miss.value.valid());
    // The unnamed entry is separate.
    auto plain = c.resolve<rtti_test_detail::IocTestPointRefl>();
    RTTI_CHECK(!plain.ok());
    RTTI_CHECK_EQ(c.count(), size_t(2));
}

RTTI_TEST_CASE(IocTestUnregisteredFailsSafely) {
    container c;
    auto r = c.resolve<rtti_test_detail::IocTestNeedScalar>();
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code != rtti::error_code::ok);
    RTTI_CHECK(!r.value.valid());
    auto g = c.get<rtti_test_detail::IocTestNeedScalar>();
    RTTI_CHECK(!g.ok());
}

RTTI_TEST_CASE(IocTestDependencyInjection) {
    container c;
    c.register_type<rtti_test_detail::IocTestService>(lifetime::singleton);
    c.register_type<rtti_test_detail::IocTestConsumer>(lifetime::transient);
    auto svc = c.resolve<rtti_test_detail::IocTestService>();
    RTTI_CHECK(svc.ok());
    auto con = c.resolve<rtti_test_detail::IocTestConsumer>();
    RTTI_CHECK(con.ok());
    RTTI_CHECK(con.value.valid());
    RTTI_CHECK(con.value->srv == svc.value.get());
}

RTTI_TEST_CASE(IocTestDependencyInjectionScalarDefault) {
    // Optional scalar stays defaulted while the dependency is injected.
    container c;
    c.register_type<rtti_test_detail::IocTestService>(lifetime::singleton);
    c.register_type<rtti_test_detail::IocTestConsumer2>(lifetime::transient);
    auto con = c.resolve<rtti_test_detail::IocTestConsumer2>();
    RTTI_CHECK(con.ok());
    RTTI_CHECK(con.value->srv != nullptr);
    RTTI_CHECK_EQ(con.value->count, 0);
}

RTTI_TEST_CASE(IocTestMissingRequiredScalarFails) {
    container c;
    c.register_type<rtti_test_detail::IocTestNeedScalar>(lifetime::transient);
    auto r = c.resolve<rtti_test_detail::IocTestNeedScalar>();
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code == rtti::error_code::missing_key);
    RTTI_CHECK(!r.value.valid());
}

RTTI_TEST_CASE(IocTestOptionalScalarDefaults) {
    container c;
    c.register_type<rtti_test_detail::IocTestRanged>(lifetime::transient);
    auto r = c.resolve<rtti_test_detail::IocTestRanged>();
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(r.value->lo, 0);
    RTTI_CHECK_EQ(r.value->hi, 0);
}

RTTI_TEST_CASE(IocTestInterfaceToImplSingleton) {
    container c;
    c.register_as<rtti_test_detail::IocTestWidgetView, rtti_test_detail::IocTestWidgetImpl>(
        lifetime::singleton);
    auto v = c.resolve<rtti_test_detail::IocTestWidgetView>();
    RTTI_CHECK(v.ok());
    RTTI_CHECK(v.value.valid());
    RTTI_CHECK_EQ(v.value->value(), 0);
    v.value->bump();
    RTTI_CHECK_EQ(v.value->value(), 1);
    auto v2 = c.resolve<rtti_test_detail::IocTestWidgetView>();
    RTTI_CHECK(v2.ok());
    RTTI_CHECK(v.value.get() == v2.value.get());
}

RTTI_TEST_CASE(IocTestInterfaceToImplTransient) {
    container c;
    c.register_as<rtti_test_detail::IocTestWidgetView, rtti_test_detail::IocTestWidgetImpl>(
        lifetime::transient);
    auto a = c.resolve<rtti_test_detail::IocTestWidgetView>();
    auto b = c.resolve<rtti_test_detail::IocTestWidgetView>();
    RTTI_CHECK(a.ok());
    RTTI_CHECK(b.ok());
    RTTI_CHECK(a.value.get() != b.value.get());
}

RTTI_TEST_CASE(IocTestFactorySingletonSameInstance) {
    container c;
    c.register_factory<rtti_test_detail::IocTestMade>(
        [](container&) -> std::unique_ptr<rtti_test_detail::IocTestMade> {
            auto p = std::unique_ptr<rtti_test_detail::IocTestMade>(
                new rtti_test_detail::IocTestMade());
            p->tag = 77;
            return p;
        },
        lifetime::singleton);
    auto a = c.resolve<rtti_test_detail::IocTestMade>();
    auto b = c.resolve<rtti_test_detail::IocTestMade>();
    RTTI_CHECK(a.ok());
    RTTI_CHECK(b.ok());
    RTTI_CHECK_EQ(a.value->tag, 77);
    RTTI_CHECK(a.value.get() == b.value.get());
}

RTTI_TEST_CASE(IocTestFactoryTransientNewEachResolve) {
    container c;
    c.register_factory<rtti_test_detail::IocTestMade>(
        [](container&) -> std::unique_ptr<rtti_test_detail::IocTestMade> {
            auto p = std::unique_ptr<rtti_test_detail::IocTestMade>(
                new rtti_test_detail::IocTestMade());
            p->tag = 5;
            return p;
        },
        lifetime::transient);
    auto a = c.resolve<rtti_test_detail::IocTestMade>();
    auto b = c.resolve<rtti_test_detail::IocTestMade>();
    RTTI_CHECK(a.ok());
    RTTI_CHECK(b.ok());
    RTTI_CHECK(a.value.get() != b.value.get());
    RTTI_CHECK_EQ(a.value->tag, 5);
}

RTTI_TEST_CASE(IocTestFactoryCanResolveDependencies) {
    // A factory receives the container and may resolve services itself.
    container c;
    c.register_type<rtti_test_detail::IocTestService>(lifetime::singleton);
    c.register_factory<rtti_test_detail::IocTestMade>(
        [](container& self) -> std::unique_ptr<rtti_test_detail::IocTestMade> {
            auto p = std::unique_ptr<rtti_test_detail::IocTestMade>(
                new rtti_test_detail::IocTestMade());
            auto svc = self.resolve<rtti_test_detail::IocTestService>();
            p->tag = svc.ok() ? 1 : -1;
            return p;
        },
        lifetime::transient);
    auto m = c.resolve<rtti_test_detail::IocTestMade>();
    RTTI_CHECK(m.ok());
    RTTI_CHECK_EQ(m.value->tag, 1);
}

RTTI_TEST_CASE(IocTestCycleDetectionFailsSafely) {
    container c;
    c.register_type<rtti_test_detail::IocTestCycleA>(lifetime::singleton);
    c.register_type<rtti_test_detail::IocTestCycleB>(lifetime::singleton);
    auto r = c.resolve<rtti_test_detail::IocTestCycleA>();
    RTTI_CHECK(!r.ok());
    RTTI_CHECK(r.err.code != rtti::error_code::ok);
    RTTI_CHECK(!r.value.valid());
    // The resolving stack is clean after a cycle error: a later resolve works.
    c.register_type<rtti_test_detail::IocTestMade>(lifetime::transient);
    auto p = c.resolve<rtti_test_detail::IocTestMade>();
    RTTI_CHECK(p.ok());
}

RTTI_TEST_CASE(IocTestContainerDtorReleasesSingletons) {
    RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);
    {
        container c;
        c.register_type<rtti_test_detail::IocTestCounted>(lifetime::singleton);
        {
            auto r = c.resolve<rtti_test_detail::IocTestCounted>();
            RTTI_CHECK(r.ok());
            RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 1);
        }
        // The borrow handle from resolve is non-owning: instance still alive.
        RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 1);
    }
    RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);
}

// R2-2: destroying a borrowed singleton handle (a caller mistake) must not
// leave the container handing out a dead instance. The singleton is reset and
// a later resolve re-creates it, so wrong call order fails safely (spec §0)
// instead of returning a use-after-free view.
RTTI_TEST_CASE(IocTestSingletonDestroyThenResolveSafe) {
    RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);
    {
        container c;
        c.register_type<rtti_test_detail::IocTestCounted>(lifetime::singleton);
        auto a = c.resolve<rtti_test_detail::IocTestCounted>();
        RTTI_CHECK(a.ok());
        RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 1);

        // User destroy() on the borrowed singleton handle: the instance is
        // gone and the handle is dead...
        a.value.destroy();
        RTTI_CHECK(!a.value.valid());
        RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);

        // ...but the container must not hand out the destroyed object: the
        // next resolve re-creates the singleton (a live, distinct instance).
        auto b = c.resolve<rtti_test_detail::IocTestCounted>();
        RTTI_CHECK(b.ok());
        RTTI_CHECK(b.value.valid());
        RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 1);
        RTTI_CHECK(b.value.get() != a.value.get());

        // Container teardown releases the re-created singleton exactly once.
    }
    RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);
}

// R2-2: a destroyed singleton must not be destroyed AGAIN by the container's
// teardown (the old allocation is freed without running the destructor a
// second time). An over-decrement would drive the counter negative.
RTTI_TEST_CASE(IocTestDestroyedSingletonNotTwiceDestroyed) {
    RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);
    {
        container c;
        c.register_type<rtti_test_detail::IocTestCounted>(lifetime::singleton);
        auto a = c.resolve<rtti_test_detail::IocTestCounted>();
        RTTI_CHECK(a.ok());
        a.value.destroy();
        RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);
        // No re-resolve: the container still owns the (now dead) singleton
        // memory and must free it without running ~T() again.
    }
    RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);
}

// R2-2: destroy + resolve + destroy must behave identically every cycle.
RTTI_TEST_CASE(IocTestSingletonDestroyRepeatSafe) {
    RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);
    {
        container c;
        c.register_type<rtti_test_detail::IocTestCounted>(lifetime::singleton);
        for (int i = 0; i < 32; ++i) {
            auto a = c.resolve<rtti_test_detail::IocTestCounted>();
            RTTI_CHECK(a.ok());
            RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 1);
            a.value.destroy();
            RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);
        }
        // Teardown finds a dead singleton and frees it without double-dtor.
    }
    RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);
}

RTTI_TEST_CASE(IocTestTransientHandleOwnsInstance) {
    RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);
    container c;
    c.register_type<rtti_test_detail::IocTestCounted>(lifetime::transient);
    {
        auto r = c.resolve<rtti_test_detail::IocTestCounted>();
        RTTI_CHECK(r.ok());
        RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 1);
    }
    RTTI_CHECK_EQ(rtti_test_detail::IocTestCounted::live, 0);
}

RTTI_TEST_CASE(IocTestObjectRefFullOps) {
    // resolve returns object_ref<T>; the full task-M operation set works.
    container c;
    c.register_type<rtti_test_detail::IocTestPointRefl>(lifetime::singleton);
    auto r = c.resolve<rtti_test_detail::IocTestPointRefl>();
    RTTI_CHECK(r.ok());
    r.value->set(3, 4);

    rtti::field_value fv;
    RTTI_CHECK_EQ(r.value.get("x", fv), rtti::error_code::ok);
    RTTI_CHECK_EQ(fv.as_integer(), int64_t(3));
    RTTI_CHECK_EQ(r.value.set("y", rtti::field_value::of(int64_t(9))),
                  rtti::error_code::ok);
    RTTI_CHECK_EQ(r.value->y, 9);
    RTTI_CHECK_EQ(r.value.get("nope", fv), rtti::error_code::no_such_field);

    const rtti::type_desc* td = r.value.type_of();
    RTTI_CHECK(td != nullptr);
    RTTI_CHECK(td->kind == rtti::type_kind::reflected);
    RTTI_CHECK_EQ(td->as_reflected->field_count, size_t(2));

    std::size_t n = 0;
    for (auto it = rtti::field_begin<rtti_test_detail::IocTestPointRefl>();
         it != rtti::field_end<rtti_test_detail::IocTestPointRefl>(); ++it) {
        RTTI_CHECK(*it != nullptr);
        ++n;
    }
    RTTI_CHECK_EQ(n, size_t(2));

    rtti_test_lib::box out;
    RTTI_CHECK_EQ(r.value.to_value(&out), rtti::error_code::ok);
    RTTI_CHECK(out.type() == rtti::value_io::kind::object);
    rtti::value_io* nx = nullptr;
    RTTI_CHECK(out.get_key("x", nx));
    RTTI_CHECK(nx != nullptr && nx->type() == rtti::value_io::kind::integer);
}

RTTI_TEST_CASE(IocTestGetAlias) {
    container c;
    c.register_type<rtti_test_detail::IocTestMade>(lifetime::transient);
    auto r = c.get<rtti_test_detail::IocTestMade>();
    RTTI_CHECK(r.ok());
    RTTI_CHECK(r.value.valid());
}

RTTI_TEST_CASE(IocTestRegisteredAndCount) {
    container c;
    c.register_type<rtti_test_detail::IocTestMade>(lifetime::singleton);
    RTTI_CHECK(c.registered<rtti_test_detail::IocTestMade>());
    RTTI_CHECK(!c.registered<rtti_test_detail::IocTestWidgetView>());
    RTTI_CHECK_EQ(c.count(), size_t(1));
}

} // namespace rtti_test_lib