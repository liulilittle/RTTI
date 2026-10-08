// Memory arena implementation. English comments only.

#include <rtti/memory.hpp>

#include <cstdlib>
#include <new>

namespace rtti {

namespace {

// Round size up to a multiple of alignment (power of two).
size_t align_up(size_t size, size_t align) noexcept {
    return (size + align - 1) & ~(align - 1);
}

} // namespace

memory_arena::~memory_arena() {
    free_blocks();
}

memory_arena::memory_arena(memory_arena&& other) noexcept
    : head_(other.head_),
      tail_(other.tail_),
      cursor_(other.cursor_),
      remaining_(other.remaining_),
      total_(other.total_) {
    other.head_ = nullptr;
    other.tail_ = nullptr;
    other.cursor_ = nullptr;
    other.remaining_ = 0;
    other.total_ = 0;
}

memory_arena& memory_arena::operator=(memory_arena&& other) noexcept {
    if (this != &other) {
        free_blocks();
        head_ = other.head_;
        tail_ = other.tail_;
        cursor_ = other.cursor_;
        remaining_ = other.remaining_;
        total_ = other.total_;
        other.head_ = nullptr;
        other.tail_ = nullptr;
        other.cursor_ = nullptr;
        other.remaining_ = 0;
        other.total_ = 0;
    }
    return *this;
}

memory_arena::block* memory_arena::allocate_block(size_t size) {
    const size_t cap = size > kDefaultBlock ? size : kDefaultBlock;
    block* b = nullptr;
    const size_t header = align_up(sizeof(block), alignof(std::max_align_t));
    void* raw = std::malloc(header + cap);
    if (raw == nullptr) {
        return nullptr;
    }
    b = static_cast<block*>(raw);
    b->data = static_cast<char*>(raw) + header;
    b->used = 0;
    b->cap = cap;
    b->next = nullptr;
    total_ += header + cap;
    return b;
}

void memory_arena::free_blocks() noexcept {
    block* b = head_;
    while (b != nullptr) {
        block* next = b->next;
        std::free(b);
        b = next;
    }
}

void* memory_arena::alloc(size_t size, size_t align) {
    if (size == 0) {
        return nullptr;
    }
    if (align < alignof(std::max_align_t)) {
        align = alignof(std::max_align_t);
    }
    // Ensure the current block's cursor stays aligned.
    uintptr_t cursor = reinterpret_cast<uintptr_t>(cursor_);
    size_t pad = (align - (cursor % align)) % align;

    if (tail_ == nullptr || remaining_ < pad + size) {
        size_t want = size + align - 1;
        block* b = allocate_block(want);
        if (b == nullptr) {
            return nullptr;
        }
        if (tail_ != nullptr) {
            tail_->next = b;
        } else {
            head_ = b;
        }
        tail_ = b;
        cursor_ = b->data;
        remaining_ = b->cap;
        b->used = 0;
        // The pad computed above derives from the previous block's cursor and
        // is meaningless for the fresh block; re-derive it from the new
        // cursor so the returned pointer honours the requested alignment.
        cursor = reinterpret_cast<uintptr_t>(cursor_);
        pad = (align - (cursor % align)) % align;
    }

    const uintptr_t start = cursor + pad;
    cursor_ = reinterpret_cast<char*>(start + size);
    remaining_ -= pad + size;
    tail_->used += pad + size;
    return reinterpret_cast<void*>(start);
}

const char* memory_arena::dup(const char* data, size_t n) {
    char* p = static_cast<char*>(alloc(n + 1, 1));
    if (p == nullptr) {
        return nullptr;
    }
    if (n != 0) {
        std::memcpy(p, data, n);
    }
    p[n] = '\0';
    return p;
}

const char* memory_arena::dup(const char* s) {
    return dup(s, std::char_traits<char>::length(s));
}

void memory_arena::reset() noexcept {
    free_blocks();
    head_ = nullptr;
    tail_ = nullptr;
    cursor_ = nullptr;
    remaining_ = 0;
    total_ = 0;
}

} // namespace rtti