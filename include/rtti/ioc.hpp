#pragma once
// IOC container / dependency injection for the RTTI reflection library.
// A thin layer over the reflective instantiation from task M (spec section
// 18): register_type<T> / register_as<Iface,Impl> / register_factory<T> feed
// a per-container registry; resolve<T> / get<T> return object_ref<T> handles.
//
// Design decisions (documented in this header):
//  - namespace rtti::ioc; the container owns every singleton it creates and
//    releases it on destruction (RAII). Transient instances are handed to the
//    caller as an owning object_ref<T>.
//  - Singleton borrow handles are non-owning views. Calling destroy() on one
//    is a caller misuse, but it fails safely (spec section 0): the destroyed
//    singleton is marked dead and the NEXT resolve<T> re-creates it, so the
//    container never hands out a use-after-free instance and never runs the
//    destructor twice (a dead singleton's block is freed without a second
//    ~T()). This contract holds only while the container lives: the borrow
//    handle does not own the instance's memory, so it must never outlive the
//    container (a destroy() after the container is gone would run ~T() on
//    freed memory).
//  - resolve<T> returns rtti::result<object_ref<T>>; every failure is an
//    error_code (never throws, never crashes): unregistered -> kind_mismatch,
//    missing required scalar -> missing_key, cycle / unsupported dependency
//    shape -> kind_mismatch.
//  - Registration matches a C++ type through typeid (std::type_index) plus an
//    optional name. register_as<Iface,Impl> requires Impl to be derived from
//    Iface (single inheritance, offset-0 base); resolve<Iface> returns the
//    Impl instance viewed as Iface (the interface should declare a virtual
//    destructor so object_ref deletion on the transient path is well defined).
//  - Dependency injection: a ctor parameter whose underlying type matches a
//    registered dependency is auto-injected. Shapes supported by default:
//      Dependency Istr (X), Istr& / X* borrow the container-owned singleton,
//      std::shared_ptr<X> shares it, and a plain X value copies it. A transient
//    dependency cannot be borrowed/shared (kind_mismatch); for a plain X the
//    transient factory creates a fresh instance that is moved into the ctor
//    argument. Non-dependency (scalar) parameters use their registered
//    optional default (value-init) or fail with missing_key when required.
//  - Cycle detection: the container keeps an in-flight type stack; re-entry of
//    the same type reports kind_mismatch instead of recursing forever.
//
// English comments only; C++17; no third-party dependencies.

#include <rtti/reflect.hpp>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <utility>
#include <vector>

namespace rtti {
namespace ioc {

// Instance lifetime policy.
enum class lifetime {
    transient, // a fresh instance per resolve; the caller owns the handle
    singleton  // created once and reused; the container owns the instance
};

namespace detail {

// The underlying type a dependency parameter refers to: strips pointers,
// references, const and std::shared_ptr so the registry (keyed by base type)
// can be consulted.
template <typename D>
struct dep_target {
    using type = std::decay_t<D>;
};
template <typename D>
struct dep_target<std::shared_ptr<D>> {
    using type = std::remove_cv_t<D>;
};
template <typename D>
struct dep_target<D*> {
    using type = std::remove_cv_t<D>;
};
template <typename D>
struct dep_target<D&> {
    using type = std::remove_cv_t<D>;
};

template <typename D>
struct is_ptr_dep : std::false_type {};
template <typename D>
struct is_ptr_dep<D*> : std::true_type {};

template <typename D>
struct is_ref_dep : std::false_type {};
template <typename D>
struct is_ref_dep<D&> : std::true_type {};

template <typename D>
struct is_shared_dep : std::false_type {};
template <typename D>
struct is_shared_dep<std::shared_ptr<D>> : std::true_type {};

// R2-2: singleton memory/deallocation helpers. A singleton's release must be
// able to free the block WITHOUT running the destructor a second time when a
// caller already destroyed the borrowed handle (~T() has run by then), so the
// block is returned through the plain (aligned-aware) deallocation function
// and the destructor is gated by the per-instance `live` flag.
#if defined(__STDCPP_DEFAULT_NEW_ALIGNMENT__)
constexpr std::size_t kDefaultNewAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
#else
constexpr std::size_t kDefaultNewAlignment = alignof(std::max_align_t);
#endif

template <typename T>
inline void raw_free_block(void* q) noexcept {
    if constexpr (alignof(T) > kDefaultNewAlignment) {
        ::operator delete(q, std::align_val_t(alignof(T)));
    } else {
        ::operator delete(q);
    }
}

// The singleton control block: the shared_ptr's deleter runs the destructor
// only while the instance is still alive (the borrowed handle's release flips
// the shared flag first), then returns the block to the allocation function.
// `live` is captured by value into the deleter so it stays valid for as long
// as any aliasing shared_ptr refs exist (it outlives the entry).
template <typename T>
inline std::shared_ptr<void> guarded_singleton(void* p, std::shared_ptr<bool> live) {
    return std::shared_ptr<void>(p, [live](void* q) noexcept {
        if (*live) {
            static_cast<T*>(q)->~T();
        }
        raw_free_block<T>(q);
    });
}

} // namespace detail

// The dependency-injection container.
class container {
public:
    container() noexcept = default;
    container(const container&) = delete;
    container& operator=(const container&) = delete;
    container(container&&) = delete;
    container& operator=(container&&) = delete;
    ~container() = default; // entries_ (unique_ptr) releases every singleton

    // --- Registration -------------------------------------------------------

    // Register T as a resolvable type. resolve<T>() builds it with its
    // reflected constructor (dependency injection) or default construction
    // when no constructor registry is present.
    template <typename T>
    void register_type(lifetime lf = lifetime::singleton) {
        register_type<T>(nullptr, lf);
    }
    template <typename T>
    void register_type(const char* name, lifetime lf) {
        auto e = std::make_unique<entry>();
        e->scope = lf;
        e->make_raw = [](container& self) -> void* {
            return self.template construct_fresh<T>();
        };
        e->make_owned =
            [](container& self)
            -> std::pair<std::shared_ptr<void>, std::shared_ptr<bool>> {
            void* p = self.template construct_fresh<T>();
            if (p == nullptr) {
                return {};
            }
            auto live = std::make_shared<bool>(true);
            return {detail::guarded_singleton<T>(p, live), std::move(live)};
        };
        e->to_view = [](void* p) { return p; };
        entries_[{std::type_index(typeid(T)), std::string(name != nullptr ? name : "")}] =
            std::move(e);
    }

    // Map an interface/abstract base to a concrete implementation.
    template <typename Iface, typename Impl>
    void register_as(lifetime lf = lifetime::singleton) {
        register_as<Iface, Impl>(nullptr, lf);
    }
    template <typename Iface, typename Impl>
    void register_as(const char* name, lifetime lf) {
        static_assert(std::is_base_of<Iface, Impl>::value,
                      "ioc::register_as requires Impl derived from Iface");
        auto e = std::make_unique<entry>();
        e->scope = lf;
        e->make_raw = [](container& self) -> void* {
            void* p = self.template construct_fresh<Impl>();
            if (p == nullptr) {
                return nullptr;
            }
            return static_cast<void*>(static_cast<Iface*>(static_cast<Impl*>(p)));
        };
        e->make_owned =
            [](container& self)
            -> std::pair<std::shared_ptr<void>, std::shared_ptr<bool>> {
            void* p = self.template construct_fresh<Impl>();
            if (p == nullptr) {
                return {};
            }
            auto live = std::make_shared<bool>(true);
            return {detail::guarded_singleton<Impl>(p, live), std::move(live)};
        };
        e->to_view = [](void* p) -> void* {
            return static_cast<void*>(static_cast<Iface*>(static_cast<Impl*>(p)));
        };
        entries_[{std::type_index(typeid(Iface)), std::string(name != nullptr ? name : "")}] =
            std::move(e);
    }

    // Register a factory function (factory receives the container).
    template <typename T>
    void register_factory(std::function<std::unique_ptr<T>(container&)> factory,
                          lifetime lf = lifetime::singleton) {
        register_factory<T>(nullptr, std::move(factory), lf);
    }
    template <typename T>
    void register_factory(const char* name,
                          std::function<std::unique_ptr<T>(container&)> factory,
                          lifetime lf) {
        auto e = std::make_unique<entry>();
        e->scope = lf;
        auto fac = std::move(factory);
        e->make_raw = [fac](container& self) -> void* {
            std::unique_ptr<T> u = fac(self);
            if (!u) {
                self.last_.code = error_code::kind_mismatch;
                return nullptr;
            }
            return u.release();
        };
        e->make_owned =
            [fac](container& self)
            -> std::pair<std::shared_ptr<void>, std::shared_ptr<bool>> {
            std::unique_ptr<T> u = fac(self);
            if (!u) {
                self.last_.code = error_code::kind_mismatch;
                return {};
            }
            T* p = u.release();
            auto live = std::make_shared<bool>(true);
            return {detail::guarded_singleton<T>(p, live), std::move(live)};
        };
        e->to_view = [](void* p) { return p; };
        entries_[{std::type_index(typeid(T)), std::string(name != nullptr ? name : "")}] =
            std::move(e);
    }

    // Resolution ----------------------------------------------------------------
    // Singleton entries return a non-owning (borrowed) view of the shared
    // instance; transient entries return a caller-owned handle.
    template <typename T>
    result<object_ref<T>> resolve() {
        return resolve<T>(nullptr);
    }
    template <typename T>
    result<object_ref<T>> resolve(const char* name) {
        result<object_ref<T>> r;
        entry* e = find(typeid(T), name);
        if (e == nullptr) {
            r.err.code = error_code::kind_mismatch;
            return r;
        }
        if (cycle(typeid(T))) {
            r.err.code = error_code::kind_mismatch;
            return r;
        }
        last_.code = error_code::ok;
        state_guard g(*this, typeid(T));
        if (e->scope == lifetime::singleton) {
            ensure_single(*e);
            if (!singleton_alive(*e)) {
                r.err = last_;
                return r;
            }
            // Borrowed view of the container-owned singleton. R2-2: a caller
            // may (wrongly) invoke destroy() on this handle; its release
            // flips the singleton's liveness flag so the container re-creates
            // on the next resolve instead of handing out a dead instance.
            // The handle holds only the liveness flag, not the instance's
            // control block: it stays safe as long as the container is alive,
            // and must not be used after the container is destroyed.
            r.value = object_ref<T>::placement(
                static_cast<T*>(e->view),
                [live = e->live](void*) noexcept { *live = false; });
        } else {
            void* v = e->make_raw(*this);
            if (v == nullptr) {
                r.err = last_;
                return r;
            }
            r.value = object_ref<T>::adopt(static_cast<T*>(v));
        }
        return r;
    }

    template <typename T>
    result<object_ref<T>> get() {
        return resolve<T>();
    }
    template <typename T>
    result<object_ref<T>> get(const char* name) {
        return resolve<T>(name);
    }

    // Query ---------------------------------------------------------------------
    template <typename T>
    bool registered() const noexcept {
        return find(typeid(T), nullptr) != nullptr;
    }
    std::size_t count() const noexcept { return entries_.size(); }

private:
    using key_t = std::pair<std::type_index, std::string>;

    struct entry {
        lifetime scope = lifetime::transient;
        // Fresh concrete instance as a caller-owned raw pointer (view address
        // for an iface mapping).
        std::function<void*(container&)> make_raw;
        // An owning shared control block (used to hold singletons / transient
        // value copies) plus the per-instance "still alive" flag the singleton
        // borrow handles flip on user destroy(). Null result means the make
        // failed (last_ set).
        std::function<std::pair<std::shared_ptr<void>, std::shared_ptr<bool>>(
            container&)>

            make_owned;
        // concrete -> requested view (identity for plain registrations).
        std::function<void*(void*)> to_view;

        std::shared_ptr<void> shared; // singleton control block
        std::shared_ptr<bool> live;   // per-instance liveness (null = none yet)
        void* view = nullptr;         // cached singleton view pointer
        bool ready = false;           // singleton finalised (live instance made)
    };

    struct state_guard {
        container& self;
        std::type_index key;
        state_guard(container& c, std::type_index k) : self(c), key(k) {
            self.resolving_.push_back(key);
        }
        ~state_guard() { self.resolving_.pop_back(); }
    };

    entry* find(std::type_index t, const char* name) noexcept {
        return const_cast<entry*>(
            const_cast<const container*>(this)->find(t, name));
    }
    const entry* find(std::type_index t, const char* name) const noexcept {
        const std::string n = name != nullptr ? name : "";
        auto it = entries_.find({t, n});
        return it != entries_.end() ? it->second.get() : nullptr;
    }

    bool cycle(std::type_index t) const noexcept {
        for (const auto& x : resolving_) {
            if (x == t) {
                return true;
            }
        }
        return false;
    }

    // Create a fresh concrete T instance, including dependency injection.
    template <typename T>
    void* construct_fresh() {
        using Tuple = typename ::rtti::ctor_registry<T>::param_types;
        if constexpr (std::tuple_size<Tuple>::value == 0) {
            return new T();
        } else {
            Tuple values;
            error_code ec = error_code::ok;
            if (!fill_values<T, Tuple>(values, ec)) {
                last_.code = ec;
                return nullptr;
            }
            return ::rtti::detail::ctor_new_impl<T, Tuple>(
                values, std::make_index_sequence<std::tuple_size<Tuple>::value>{});
        }
    }

    // Fill one ctor value from DI or its optional default.
    template <typename T, typename Tuple, std::size_t I>
    bool fill_param(Tuple& values, error_code& ec) {
        using D = std::tuple_element_t<I, Tuple>;
        using Target = typename detail::dep_target<D>::type;
        auto& slot = std::get<I>(values);
        entry* dep = find(typeid(Target), nullptr);
        if (dep != nullptr) {
            return inject_dep<D>(slot, dep, ec);
        }
        const ctor_param& prm = ::rtti::ctor_registry<T>::params[I];
        if (prm.optional) {
            slot = D{};
            return true;
        }
        ec = error_code::missing_key;
        return false;
    }

    // Recursive fill driver.
    template <typename T, typename Tuple, std::size_t I>
    struct fill_seq {
        static bool run(container& self, Tuple& values, error_code& ec) {
            constexpr std::size_t N = std::tuple_size<Tuple>::value;
            if constexpr (I >= N) {
                (void)values;
                (void)ec;
                return true;
            } else {
                if (!self.template fill_param<T, Tuple, I>(values, ec)) {
                    return false;
                }
                return fill_seq<T, Tuple, I + 1>::run(self, values, ec);
            }
        }
    };

    template <typename T, typename Tuple>
    bool fill_values(Tuple& values, error_code& ec) {
        return fill_seq<T, Tuple, 0>::run(*this, values, ec);
    }

    // Inject a dependency parameter. `slot` holds the ctor argument.
    template <typename D, typename S>
    bool inject_dep(S& slot, entry* dep, error_code& ec) {
        using Target = typename detail::dep_target<D>::type;
        if (cycle(typeid(Target))) {
            ec = error_code::kind_mismatch;
            return false;
        }
        state_guard g(*this, typeid(Target));
        if constexpr (detail::is_ptr_dep<D>::value || detail::is_ref_dep<D>::value ||
                       detail::is_shared_dep<D>::value) {
            // Borrowed / shared injection requires a reused singleton.
            if (dep->scope != lifetime::singleton) {
                ec = error_code::kind_mismatch;
                return false;
            }
            ensure_single(*dep);
            if (!singleton_alive(*dep)) {
                ec = last_or_mismatch();
                return false;
            }
            if constexpr (detail::is_ptr_dep<D>::value) {
                slot = static_cast<Target*>(dep->view);
            } else if constexpr (detail::is_ref_dep<D>::value) {
                slot = *static_cast<Target*>(dep->view);
            } else {
                slot = std::shared_ptr<Target>(dep->shared,
                                               static_cast<Target*>(dep->view));
            }
        } else {
            // Plain value: copy a singleton, or move a fresh transient.
            if (dep->scope == lifetime::singleton) {
                ensure_single(*dep);
                if (!singleton_alive(*dep)) {
                    ec = last_or_mismatch();
                    return false;
                }
                slot = *static_cast<Target*>(dep->view);
            } else {
                auto made = dep->make_owned(*this);
                if (!made.first) {
                    ec = last_or_mismatch();
                    return false;
                }
                slot = std::move(*static_cast<Target*>(made.first.get()));
            }
        }
        return true;
    }

    // True when the entry's singleton instance exists AND is still alive (the
    // borrow handle's release flips *live after a user destroy()).
    bool singleton_alive(const entry& e) const noexcept {
        return e.ready && e.live && *e.live;
    }

    void ensure_single(entry& e) {
        if (singleton_alive(e)) {
            return;
        }
        auto made = e.make_owned(*this);
        if (!made.first) {
            return; // last_ already set by the factory (kind_mismatch)
        }
        e.shared = std::move(made.first);
        e.live = std::move(made.second);
        e.view = e.to_view(e.shared.get());
        e.ready = true;
    }

    error_code last_or_mismatch() const noexcept {
        if (last_.code != error_code::ok) {
            return last_.code;
        }
        return error_code::kind_mismatch;
    }

    std::map<key_t, std::unique_ptr<entry>> entries_;
    std::vector<std::type_index> resolving_;
    error_info last_;
};

} // namespace rtti::ioc
} // namespace rtti