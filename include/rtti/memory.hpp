#pragma once
// Memory infrastructure for RTTI.
// Provides a string_view alias and a high-throughput arena allocator.
// English comments only; C++17; no third-party dependencies.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <rtti/error.hpp>
#include <type_traits>
#include <utility>

namespace rtti {

// Zero-copy string view alias. All string data owned by the parse buffer or
// the arena; views never own memory.
using string_view = std::string_view;

// Bump/block arena allocator.
//
// Memory is carved from linked blocks (default 64 KiB each). Small
// allocations bump a cursor inside the current block; oversized requests get
// their own block. The whole arena is freed at once on destruction or
// reset(), which keeps parse-time allocation costs near zero.
//
// Threading: not thread-safe; one arena per document.
class memory_arena {
public:
    memory_arena() noexcept = default;
    ~memory_arena();

    memory_arena(memory_arena&& other) noexcept;
    memory_arena& operator=(memory_arena&& other) noexcept;

    memory_arena(const memory_arena&) = delete;
    memory_arena& operator=(const memory_arena&) = delete;

    // Allocate raw memory with the requested alignment.
    // Returns nullptr when the size is zero and on allocation failure (the
    // underlying malloc returned nullptr); callers must check the result.
    void* alloc(size_t size, size_t align = alignof(std::max_align_t));

    // Allocate and value-initialize an object of type T.
    template <typename T, typename... Args>
    T* alloc_obj(Args&&... args) {
        T* p = static_cast<T*>(alloc(sizeof(T), alignof(T)));
        if (p != nullptr) {
            ::new (static_cast<void*>(p)) T(std::forward<Args>(args)...);
        }
        return p;
    }

    // Copy n bytes into the arena; returns a NUL-terminated pointer.
    const char* dup(const char* data, size_t n);
    // Copy a C string (strlen) into the arena; NUL-terminated.
    const char* dup(const char* s);

    // Free all blocks; the arena is reusable afterwards.
    void reset() noexcept;

    // Total bytes currently reserved across all blocks.
    size_t total_bytes() const noexcept { return total_; }

private:
    struct block {
        char* data;
        size_t used;
        size_t cap;
        block* next;
    };

    static constexpr size_t kDefaultBlock = 64 * 1024;

    block* allocate_block(size_t size);
    void free_blocks() noexcept;

    block* head_ = nullptr;
    block* tail_ = nullptr;
    char* cursor_ = nullptr;    // next free byte in the tail block
    size_t remaining_ = 0;      // bytes left in the tail block
    size_t total_ = 0;          // sum of block capacities
};

} // namespace rtti