// Concurrency stress tests for the two mutex-protected registries of the
// RTTI reflect-core library:
//   1. desc_registry (reflection.hpp): rtti::type_of<T>() lazily materializes
//      and registers reflected type descriptors under detail::desc_registry_mutex.
//      Concurrent materialization of the same set of custom structs must be
//      race-free and must hand every thread one identical, stable descriptor
//      pointer per type; detail::find_desc must agree after all threads join.
//   2. proto extension_registry (proto.hpp): concurrent add() on the
//      process-wide registry under its internal mutex. Identical
//      re-registration is idempotent (ok), conflicting number/name reuse is
//      rejected (proto_bad_field); after join every registered entry must be
//      visible and consistently queryable.
// Standard library only: <thread>/<atomic>/<vector>. English comments only.

#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <rtti/proto.hpp>
#include <rtti/error.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

namespace rtti_test_lib {

// ---------------------------------------------------------------------------
// Custom reflected structs used for concurrent desc_registry materialization.
// Each type is materialized by every worker thread at the same time, so the
// lazy registration path in type_of_impl (register_desc under the registry
// mutex) is hammered from kConcurThreads threads simultaneously.
// ---------------------------------------------------------------------------

struct ConcurRecA {
    int a = 1;
    std::string tag;
    RTTI_DEFINE_TYPE_INTRUSIVE(ConcurRecA, a, tag)
};

struct ConcurRecB {
    ConcurRecA inner;
    std::vector<int> nums;
    RTTI_DEFINE_TYPE_INTRUSIVE(ConcurRecB, inner, nums)
};

struct ConcurRecC {
    std::string name;
    std::vector<ConcurRecA> items;
    RTTI_DEFINE_TYPE_INTRUSIVE(ConcurRecC, name, items)
};

struct ConcurRecD {
    int x = 0;
    double y = 0.0;
    RTTI_DEFINE_TYPE_INTRUSIVE(ConcurRecD, x, y)
};

struct ConcurRecE {
    ConcurRecB nested;
    ConcurRecD point;
    RTTI_DEFINE_TYPE_INTRUSIVE(ConcurRecE, nested, point)
};

struct ConcurRecF {
    std::string label;
    int code = 0;
    RTTI_DEFINE_TYPE_INTRUSIVE(ConcurRecF, label, code)
};

// Number of worker threads used by every concurrency case below.
constexpr int kConcurThreads = 6;

} // namespace rtti_test_lib

namespace {

// Minimal reusable spin barrier so all workers start hammering the registry
// at the same moment (maximizes contention on the lazy-materialization path).
class concur_barrier {
public:
    explicit concur_barrier(int expected) noexcept : expected_(expected) {}

    void arrive_and_wait() noexcept {
        arrived_.fetch_add(1, std::memory_order_acq_rel);
        while (arrived_.load(std::memory_order_acquire) < expected_) {
            // Busy-wait on purpose: short critical sections only.
        }
    }

private:
    std::atomic<int> arrived_{0};
    int expected_;
};

} // namespace

// Case 1: concurrent materialization/registration of reflected descriptors.
// Every worker calls rtti::type_of<T>() for the same batch of types; the
// first caller per type registers under desc_registry_mutex, all others must
// observe the exact same descriptor pointer with no duplicated registration.
// The harness assertion counters are not atomic, so workers only record
// results in atomics/per-thread slots and every RTTI_CHECK runs after join.
RTTI_TEST_CASE(ConcurDescRegistryConcurrentMaterialization) {
    using rtti_test_lib::ConcurRecA;
    using rtti_test_lib::ConcurRecB;
    using rtti_test_lib::ConcurRecC;
    using rtti_test_lib::ConcurRecD;
    using rtti_test_lib::ConcurRecE;
    using rtti_test_lib::ConcurRecF;
    using rtti_test_lib::kConcurThreads;

    constexpr int kTypes = 6;

    // Per-thread first-seen descriptor pointer per type (written once by each
    // thread, read by the main thread after join -> no data race).
    std::vector<std::vector<const rtti::type_desc*>> seen(
        static_cast<std::size_t>(kConcurThreads),
        std::vector<const rtti::type_desc*>(static_cast<std::size_t>(kTypes),
                                            nullptr));
    // Atomic finding counters (no harness calls inside the workers).
    std::atomic<int> null_desc{0};
    std::atomic<int> lookup_mismatch{0};

    concur_barrier barrier(kConcurThreads);
    std::atomic<bool> go{false};

    auto worker = [&](int tid) {
        barrier.arrive_and_wait();
        while (!go.load(std::memory_order_acquire)) {
        }
        auto& out = seen[static_cast<std::size_t>(tid)];
        for (int round = 0; round < 32; ++round) {
            const rtti::type_desc* d0 = rtti::type_of<ConcurRecA>();
            const rtti::type_desc* d1 = rtti::type_of<ConcurRecB>();
            const rtti::type_desc* d2 = rtti::type_of<ConcurRecC>();
            const rtti::type_desc* d3 = rtti::type_of<ConcurRecD>();
            const rtti::type_desc* d4 = rtti::type_of<ConcurRecE>();
            const rtti::type_desc* d5 = rtti::type_of<ConcurRecF>();
            const rtti::type_desc* f0 =
                rtti::detail::find_desc(typeid(ConcurRecA));
            const rtti::type_desc* f1 =
                rtti::detail::find_desc(typeid(ConcurRecB));
            const rtti::type_desc* f2 =
                rtti::detail::find_desc(typeid(ConcurRecC));
            const rtti::type_desc* f3 =
                rtti::detail::find_desc(typeid(ConcurRecD));
            const rtti::type_desc* f4 =
                rtti::detail::find_desc(typeid(ConcurRecE));
            const rtti::type_desc* f5 =
                rtti::detail::find_desc(typeid(ConcurRecF));
            if (round == 0) {
                out[0] = d0;
                out[1] = d1;
                out[2] = d2;
                out[3] = d3;
                out[4] = d4;
                out[5] = d5;
            }
            // type_of<Reflected> registers eagerly on first materialization,
            // so the locked lookup must agree from the very first round on.
            if (d0 == nullptr || d1 == nullptr || d2 == nullptr ||
                d3 == nullptr || d4 == nullptr || d5 == nullptr) {
                null_desc.fetch_add(1, std::memory_order_relaxed);
            }
            if (f0 != d0 || f1 != d1 || f2 != d2 || f3 != d3 || f4 != d4 ||
                f5 != d5) {
                lookup_mismatch.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(kConcurThreads));
    for (int t = 0; t < kConcurThreads; ++t) {
        threads.emplace_back(worker, t);
    }
    go.store(true, std::memory_order_release);
    for (std::thread& th : threads) {
        th.join();
    }

    // Joined assertion 1: no worker ever observed a null descriptor or a
    // find_desc() disagreement during the concurrent storm.
    RTTI_CHECK_EQ(null_desc.load(), 0);
    RTTI_CHECK_EQ(lookup_mismatch.load(), 0);

    // Joined assertion 2: every thread materialized every type to one
    // identical pointer (a single registry entry, no duplicated instance).
    const rtti::type_desc* expected[kTypes] = {
        rtti::type_of<ConcurRecA>(), rtti::type_of<ConcurRecB>(),
        rtti::type_of<ConcurRecC>(), rtti::type_of<ConcurRecD>(),
        rtti::type_of<ConcurRecE>(), rtti::type_of<ConcurRecF>(),
    };
    for (int i = 0; i < kTypes; ++i) {
        RTTI_CHECK(expected[i] != nullptr);
        for (int t = 0; t < kConcurThreads; ++t) {
            const rtti::type_desc* got =
                seen[static_cast<std::size_t>(t)][static_cast<std::size_t>(i)];
            RTTI_CHECK(got != nullptr);
            RTTI_CHECK_EQ(got, expected[i]);
        }
    }

    // Joined assertion 3: the mutex-protected lookup resolves the same
    // registered descriptor for every type after the join.
    RTTI_CHECK(rtti::detail::find_desc(typeid(ConcurRecA)) == expected[0]);
    RTTI_CHECK(rtti::detail::find_desc(typeid(ConcurRecB)) == expected[1]);
    RTTI_CHECK(rtti::detail::find_desc(typeid(ConcurRecC)) == expected[2]);
    RTTI_CHECK(rtti::detail::find_desc(typeid(ConcurRecD)) == expected[3]);
    RTTI_CHECK(rtti::detail::find_desc(typeid(ConcurRecE)) == expected[4]);
    RTTI_CHECK(rtti::detail::find_desc(typeid(ConcurRecF)) == expected[5]);
}

// Case 2: concurrent writes into the proto extension registry. Storm layout:
//   * phase A -- all threads add the same 5 descriptors (numbers 5001..5005):
//     idempotent re-registration, every add must return ok;
//   * phase B -- every thread adds its own 10 unique numbers/names
//     (6000 + t*100 + k): every add must return ok;
//   * phase C -- all threads race the same field number 7001 with two
//     different names/layouts: exactly one side wins (all its adds return
//     ok), the losing side must uniformly get proto_bad_field.
// After the join the registry size must equal the number of accepted unique
// registrations, every accepted entry must be visible with a stable pointer
// under repeated find() calls, and find_by_name() must agree with find().
RTTI_TEST_CASE(ConcurProtoRegistryConcurrentWrites) {
    using rtti::error_code;
    using rtti::proto::extension_desc;
    using rtti::proto::extension_registry;
    using rtti_test_lib::kConcurThreads;

    extension_registry::global().clear();
    RTTI_CHECK_EQ(extension_registry::global().size(), std::size_t(0));

    std::atomic<int> shared_ok{0};
    std::atomic<int> unique_ok{0};
    std::atomic<int> clash_a_ok{0};
    std::atomic<int> clash_b_ok{0};
    std::atomic<int> unexpected{0};
    std::atomic<int> clash_rejects{0};
    std::atomic<int> clash_reject_code{-1};

    concur_barrier barrier(kConcurThreads);

    auto worker = [&](int tid) {
        barrier.arrive_and_wait();

        // Phase A: identical descriptors across all threads -> idempotent ok.
        for (std::uint32_t k = 0; k < 5; ++k) {
            extension_desc d;
            d.number = 5001u + k;
            d.name = "concur_shared_" + std::to_string(k);
            d.wire = rtti::proto::wire_type::varint;
            const error_code e = extension_registry::global().add(d);
            if (e == error_code::ok) {
                shared_ok.fetch_add(1, std::memory_order_relaxed);
            } else {
                unexpected.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Phase B: per-thread unique numbers/names -> all must be ok.
        for (std::uint32_t k = 0; k < 10; ++k) {
            extension_desc d;
            d.number = 6000u + static_cast<std::uint32_t>(tid) * 100u + k;
            d.name =
                "concur_t" + std::to_string(tid) + "_" + std::to_string(k);
            d.wire = rtti::proto::wire_type::varint;
            const error_code e = extension_registry::global().add(d);
            if (e == error_code::ok) {
                unique_ok.fetch_add(1, std::memory_order_relaxed);
            } else {
                unexpected.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Phase C: same number, two different layouts -> exactly one wins.
        // The losing side must uniformly receive proto_bad_field.
        extension_desc clash;
        clash.number = 7001u;
        if (tid % 2 == 0) {
            clash.name = "concur_clash_a";
            clash.wire = rtti::proto::wire_type::varint;
            const error_code e = extension_registry::global().add(clash);
            if (e == error_code::ok) {
                clash_a_ok.fetch_add(1, std::memory_order_relaxed);
            } else {
                clash_rejects.fetch_add(1, std::memory_order_relaxed);
                clash_reject_code.store(static_cast<int>(e),
                                        std::memory_order_relaxed);
            }
        } else {
            clash.name = "concur_clash_b";
            clash.wire = rtti::proto::wire_type::fixed32;
            const error_code e = extension_registry::global().add(clash);
            if (e == error_code::ok) {
                clash_b_ok.fetch_add(1, std::memory_order_relaxed);
            } else {
                clash_rejects.fetch_add(1, std::memory_order_relaxed);
                clash_reject_code.store(static_cast<int>(e),
                                        std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(kConcurThreads));
    for (int t = 0; t < kConcurThreads; ++t) {
        threads.emplace_back(worker, t);
    }
    for (std::thread& th : threads) {
        th.join();
    }

    // Joined assertion 1: shared + unique adds always succeeded; rejections
    // only happened in the clash and always as proto_bad_field.
    RTTI_CHECK_EQ(unexpected.load(), 0);
    RTTI_CHECK_EQ(clash_reject_code.load(),
                  static_cast<int>(error_code::proto_bad_field));
    RTTI_CHECK_EQ(shared_ok.load(), kConcurThreads * 5);
    RTTI_CHECK_EQ(unique_ok.load(), kConcurThreads * 10);

    // Joined assertion 2: the clash resolved to exactly one winning layout.
    RTTI_CHECK(clash_a_ok.load() == kConcurThreads / 2 ||
               clash_a_ok.load() == 0);
    RTTI_CHECK(clash_b_ok.load() == kConcurThreads - kConcurThreads / 2 ||
               clash_b_ok.load() == 0);
    RTTI_CHECK(clash_a_ok.load() > 0 || clash_b_ok.load() > 0);
    RTTI_CHECK_EQ(clash_rejects.load(),
                  kConcurThreads - (clash_a_ok.load() > 0
                                        ? clash_a_ok.load()
                                        : clash_b_ok.load()));

    // Joined assertion 3: registry size equals the number of accepted unique
    // registrations (5 shared + per-thread unique + exactly 1 clash winner).
    const std::size_t expected_size =
        5u + static_cast<std::size_t>(kConcurThreads) * 10u + 1u;
    RTTI_CHECK_EQ(extension_registry::global().size(), expected_size);

    // Joined assertion 4: every accepted entry is visible and consistent.
    for (std::uint32_t k = 0; k < 5; ++k) {
        const extension_desc* d = extension_registry::global().find(5001u + k);
        RTTI_CHECK(d != nullptr);
        if (d != nullptr) {
            RTTI_CHECK_EQ(d->name, "concur_shared_" + std::to_string(k));
        }
        const extension_desc* by_name =
            extension_registry::global().find_by_name("concur_shared_" +
                                                      std::to_string(k));
        RTTI_CHECK(by_name == d);
    }
    for (int t = 0; t < kConcurThreads; ++t) {
        for (std::uint32_t k = 0; k < 10; ++k) {
            const std::uint32_t number =
                6000u + static_cast<std::uint32_t>(t) * 100u + k;
            const std::string name =
                "concur_t" + std::to_string(t) + "_" + std::to_string(k);
            const extension_desc* d =
                extension_registry::global().find(number);
            RTTI_CHECK(d != nullptr);
            if (d != nullptr) {
                RTTI_CHECK_EQ(d->name, name);
            }
            const extension_desc* by_name =
                extension_registry::global().find_by_name(name);
            RTTI_CHECK(by_name == d);
        }
    }
    const extension_desc* clash_winner =
        extension_registry::global().find(7001u);
    RTTI_CHECK(clash_winner != nullptr);
    if (clash_winner != nullptr) {
        if (clash_a_ok.load() > 0) {
            RTTI_CHECK_EQ(clash_winner->name, "concur_clash_a");
            RTTI_CHECK(extension_registry::global().find_by_name(
                           "concur_clash_b") == nullptr);
        } else {
            RTTI_CHECK_EQ(clash_winner->name, "concur_clash_b");
            RTTI_CHECK(extension_registry::global().find_by_name(
                           "concur_clash_a") == nullptr);
        }
    }

    // Joined assertion 5: idempotent re-registration keeps the size stable.
    extension_desc again;
    again.number = 5001u;
    again.name = "concur_shared_0";
    again.wire = rtti::proto::wire_type::varint;
    RTTI_CHECK_EQ(extension_registry::global().add(again), error_code::ok);
    RTTI_CHECK_EQ(extension_registry::global().size(), expected_size);

    // Test isolation: leave the process-wide registry empty again.
    extension_registry::global().clear();
    RTTI_CHECK_EQ(extension_registry::global().size(), std::size_t(0));
}

// Case 3: mixed readers + writers on the extension registry. Writers keep
// registering (bounded workload) while readers hammer find/find_by_name
// concurrently; readers must only ever observe consistent entries (number
// and name always pair up, pointers stay stable). Each writer flips a done
// flag when its loop ends (success or not) and readers stop once all writers
// are done, so an unexpected add rejection fails the joined assertions
// instead of deadlocking the reader spin.
RTTI_TEST_CASE(ConcurProtoRegistryMixedReadersWriters) {
    using rtti::error_code;
    using rtti::proto::extension_desc;
    using rtti::proto::extension_registry;
    using rtti_test_lib::kConcurThreads;

    extension_registry::global().clear();
    RTTI_CHECK_EQ(extension_registry::global().size(), std::size_t(0));

    std::atomic<bool> go{false};
    std::atomic<bool> done{false};
    std::atomic<int> registered_ok{0};
    std::atomic<int> writer_rejects{0};
    std::atomic<int> reader_violations{0};

    const int writers = kConcurThreads / 2;
    const int readers = kConcurThreads - writers;
    const int total_expected = writers * 50;

    auto writer = [&](int wid) {
        while (!go.load(std::memory_order_acquire)) {
        }
        for (std::uint32_t k = 0; k < 50; ++k) {
            extension_desc d;
            d.number = 8000u + static_cast<std::uint32_t>(wid) * 100u + k;
            d.name = "concur_mixed_w" + std::to_string(wid) + "_" +
                     std::to_string(k);
            d.wire = rtti::proto::wire_type::varint;
            const error_code e = extension_registry::global().add(d);
            if (e == error_code::ok) {
                registered_ok.fetch_add(1, std::memory_order_relaxed);
            } else {
                writer_rejects.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // Unconditionally release the readers whether the workload completed
        // or adds were rejected; the joined assertions judge the outcome.
        done.store(true, std::memory_order_release);
    };

    auto reader = [&]() {
        while (!go.load(std::memory_order_acquire)) {
        }
        // Stop once every writer finished (all done flags set, or the
        // expected total was reached); until then every scanned number must
        // pair with its name and every find() result must be stable under
        // find_by_name().
        while (!done.load(std::memory_order_acquire) &&
               registered_ok.load(std::memory_order_acquire) < total_expected) {
            for (std::uint32_t wid = 0;
                 wid < static_cast<std::uint32_t>(writers); ++wid) {
                for (std::uint32_t k = 0; k < 50; ++k) {
                    const std::uint32_t number = 8000u + wid * 100u + k;
                    const extension_desc* d =
                        extension_registry::global().find(number);
                    if (d != nullptr) {
                        const std::string want = "concur_mixed_w" +
                                                 std::to_string(wid) + "_" +
                                                 std::to_string(k);
                        if (d->name != want) {
                            reader_violations.fetch_add(
                                1, std::memory_order_relaxed);
                        }
                        const extension_desc* by_name =
                            extension_registry::global().find_by_name(want);
                        if (by_name != d) {
                            reader_violations.fetch_add(
                                1, std::memory_order_relaxed);
                        }
                    }
                }
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(kConcurThreads));
    for (int t = 0; t < writers; ++t) {
        threads.emplace_back(writer, t);
    }
    for (int t = 0; t < readers; ++t) {
        threads.emplace_back(reader);
    }
    go.store(true, std::memory_order_release);
    for (std::thread& th : threads) {
        th.join();
    }

    // Joined assertion 1: no add was rejected, no reader ever saw an
    // inconsistent number/name pair or an unstable pointer.
    RTTI_CHECK_EQ(writer_rejects.load(), 0);
    RTTI_CHECK_EQ(reader_violations.load(), 0);

    // Joined assertion 2: registry size equals the number of successful
    // registrations (no lost writes, no duplicated entries).
    RTTI_CHECK_EQ(registered_ok.load(), total_expected);
    RTTI_CHECK_EQ(extension_registry::global().size(),
                  static_cast<std::size_t>(total_expected));

    // Joined assertion 3: every written entry is visible after the join.
    for (int wid = 0; wid < writers; ++wid) {
        for (std::uint32_t k = 0; k < 50; ++k) {
            const std::uint32_t number =
                8000u + static_cast<std::uint32_t>(wid) * 100u + k;
            const std::string want = "concur_mixed_w" + std::to_string(wid) +
                                     "_" + std::to_string(k);
            const extension_desc* d =
                extension_registry::global().find(number);
            RTTI_CHECK(d != nullptr);
            if (d != nullptr) {
                RTTI_CHECK_EQ(d->name, want);
            }
        }
    }

    // Test isolation: leave the process-wide registry empty again.
    extension_registry::global().clear();
    RTTI_CHECK_EQ(extension_registry::global().size(), std::size_t(0));
}
