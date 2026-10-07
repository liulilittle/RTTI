#include <rtti/flat.hpp>

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace rtti {
namespace flat {

// ---------------------------------------------------------------------------
// Owned value_io tree (document). A document owns a pool of nodes; every node
// records its owning document so nested nodes can allocate their children.
// ---------------------------------------------------------------------------

struct document::node : public rtti::value_io {
    rtti::value_io::kind k = rtti::value_io::kind::null;
    bool b = false;
    std::int64_t i = 0;
    double d = 0.0;
    std::string s;
    std::vector<node*> arr;
    std::vector<std::pair<std::string, node*>> obj;
    document* owner = nullptr;

    node() noexcept = default;

    rtti::value_io::kind type() const noexcept override { return k; }
    bool as_bool(bool& out) const noexcept override {
        if (k != rtti::value_io::kind::boolean) {
            return false;
        }
        out = b;
        return true;
    }
    bool as_integer(std::int64_t& out) const noexcept override {
        if (k != rtti::value_io::kind::integer) {
            return false;
        }
        out = i;
        return true;
    }
    bool as_real(double& out) const noexcept override {
        if (k == rtti::value_io::kind::integer) {
            out = static_cast<double>(i);
            return true;
        }
        if (k != rtti::value_io::kind::real) {
            return false;
        }
        out = d;
        return true;
    }
    bool as_string(rtti::string_view& out) const noexcept override {
        if (k != rtti::value_io::kind::string) {
            return false;
        }
        out = rtti::string_view(s.data(), s.size());
        return true;
    }
    std::size_t size() const noexcept override {
        return k == rtti::value_io::kind::array   ? arr.size()
               : k == rtti::value_io::kind::object ? obj.size()
                                                   : 0;
    }
    rtti::value_io* at(std::size_t index) override {
        return (k == rtti::value_io::kind::array && index < arr.size())
                   ? arr[index]
                   : nullptr;
    }
    rtti::value_io* at(std::size_t index) const override {
        return (k == rtti::value_io::kind::array && index < arr.size())
                   ? arr[index]
                   : nullptr;
    }
    bool get_key(rtti::string_view key, rtti::value_io*& out) override {
        return get_key_impl(key, out);
    }
    bool get_key(rtti::string_view key, rtti::value_io*& out) const override {
        return get_key_impl(key, out);
    }
    bool get_key_at(std::size_t index, rtti::string_view& key,
                    rtti::value_io*& out) const override {
        if (k != rtti::value_io::kind::object || index >= obj.size()) {
            return false;
        }
        key = rtti::string_view(obj[index].first.data(),
                                obj[index].first.size());
        out = obj[index].second;
        return true;
    }
    rtti::value_io* make_array() override {
        k = rtti::value_io::kind::array;
        arr.clear();
        obj.clear();
        return this;
    }
    void push_back(rtti::value_io* v) override {
        if (k == rtti::value_io::kind::array) {
            arr.push_back(static_cast<node*>(v));
        }
    }
    rtti::value_io* make_object() override {
        k = rtti::value_io::kind::object;
        arr.clear();
        obj.clear();
        return this;
    }
    void set_key(rtti::string_view key, rtti::value_io* v) override {
        if (k != rtti::value_io::kind::object) {
            return;
        }
        obj.emplace_back(std::string(key.data(), key.size()),
                         static_cast<node*>(v));
    }
    rtti::value_io* make_scalar(rtti::field_value v) override {
        k = rtti::value_io::kind::null;
        b = false;
        i = 0;
        d = 0.0;
        s.clear();
        switch (v.type()) {
            case rtti::field_value::kind::boolean:
                k = rtti::value_io::kind::boolean;
                b = v.as_bool();
                break;
            case rtti::field_value::kind::integer:
                k = rtti::value_io::kind::integer;
                i = v.as_integer();
                break;
            case rtti::field_value::kind::real:
                k = rtti::value_io::kind::real;
                d = v.as_real();
                break;
            case rtti::field_value::kind::string: {
                const rtti::string_view sv = v.as_string();
                s.assign(sv.data(), sv.size());
                k = rtti::value_io::kind::string;
                break;
            }
            default:
                break; // none/object -> null
        }
        return this;
    }
    rtti::value_io* make_node() override {
        return owner != nullptr ? owner->alloc_node() : nullptr;
    }

private:
    bool get_key_impl(rtti::string_view key, rtti::value_io*& out) const {
        if (k != rtti::value_io::kind::object) {
            return false;
        }
        for (const auto& e : obj) {
            if (rtti::string_view(e.first.data(), e.first.size()) == key) {
                out = e.second;
                return true;
            }
        }
        return false;
    }
};

namespace {
// Round an offset up to a multiple of align (0 < align <= 256).
inline std::size_t round_up(std::size_t v, std::size_t align) noexcept {
    const std::size_t r = v % align;
    return r ? v + (align - r) : v;
}
// True when id points to exactly four non-NUL characters (a well-formed
// 4-byte file identifier literal). The scan stops at the literal's NUL, so a
// short identifier never reads past its own storage.
inline bool is_id4(const char* id) noexcept {
    return id != nullptr && id[0] != '\0' && id[1] != '\0' && id[2] != '\0' &&
           id[3] != '\0';
}
} // namespace

document::document() {
    root_ = alloc_node();
    root_->make_object();
}

document::document(document&& o) noexcept
    : pool_(std::move(o.pool_)), root_(o.root_) {
    o.root_ = nullptr;
    for (auto& p : pool_) {
        if (p) {
            p->owner = this;
        }
    }
}

document& document::operator=(document&& o) noexcept {
    if (this != &o) {
        reset();
        pool_ = std::move(o.pool_);
        root_ = o.root_;
        o.root_ = nullptr;
        for (auto& p : pool_) {
            if (p) {
                p->owner = this;
            }
        }
    }
    return *this;
}

document::~document() { reset(); }

void document::reset() noexcept {
    pool_.clear();
    root_ = nullptr;
}

document::node* document::alloc_node() {
    pool_.push_back(std::unique_ptr<node>(new node()));
    node* n = pool_.back().get();
    n->owner = this;
    return n;
}

rtti::value_io::kind document::type() const noexcept { return root_->type(); }
bool document::as_bool(bool& out) const noexcept { return root_->as_bool(out); }
bool document::as_integer(std::int64_t& out) const noexcept {
    return root_->as_integer(out);
}
bool document::as_real(double& out) const noexcept { return root_->as_real(out); }
bool document::as_string(rtti::string_view& out) const noexcept {
    return root_->as_string(out);
}
std::size_t document::size() const noexcept { return root_->size(); }
rtti::value_io* document::at(std::size_t index) { return root_->at(index); }
rtti::value_io* document::at(std::size_t index) const { return root_->at(index); }
bool document::get_key(rtti::string_view key, rtti::value_io*& out) {
    return root_->get_key(key, out);
}
bool document::get_key(rtti::string_view key, rtti::value_io*& out) const {
    return root_->get_key(key, out);
}
bool document::get_key_at(std::size_t index, rtti::string_view& key,
                          rtti::value_io*& out) const {
    return root_->get_key_at(index, key, out);
}
rtti::value_io* document::make_array() { return root_->make_array(); }
void document::push_back(rtti::value_io* v) { root_->push_back(v); }
rtti::value_io* document::make_object() { return root_->make_object(); }
void document::set_key(rtti::string_view key, rtti::value_io* v) {
    root_->set_key(key, v);
}
rtti::value_io* document::make_scalar(rtti::field_value v) {
    return root_->make_scalar(v);
}
rtti::value_io* document::make_node() { return alloc_node(); }

// ---------------------------------------------------------------------------
// Wire helpers
// ---------------------------------------------------------------------------

namespace detail {

// Bounded little-endian reader over a source buffer. Every access checks the
// bounds first, so malformed input never reads out of range.
struct fbuf {
    const unsigned char* p;
    std::size_t n;

    explicit fbuf(rtti::string_view src) noexcept
        : p(reinterpret_cast<const unsigned char*>(src.data())), n(src.size()) {}

    bool in_bounds(std::size_t pos, std::size_t count) const noexcept {
        return count <= n && pos <= n - count;
    }

    std::uint8_t u8(std::size_t pos, bool& ok) const noexcept {
        if (!in_bounds(pos, 1)) {
            ok = false;
            return 0;
        }
        return p[pos];
    }
    std::uint16_t u16(std::size_t pos, bool& ok) const noexcept {
        if (!in_bounds(pos, 2)) {
            ok = false;
            return 0;
        }
        return static_cast<std::uint16_t>(p[pos] |
                                          (static_cast<std::uint16_t>(p[pos + 1])
                                           << 8));
    }
    std::uint32_t u32(std::size_t pos, bool& ok) const noexcept {
        if (!in_bounds(pos, 4)) {
            ok = false;
            return 0;
        }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(p[pos + i]) << (8 * i);
        }
        return v;
    }
    std::uint64_t u64(std::size_t pos, bool& ok) const noexcept {
        if (!in_bounds(pos, 8)) {
            ok = false;
            return 0;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<std::uint64_t>(p[pos + i]) << (8 * i);
        }
        return v;
    }
    std::int32_t i32(std::size_t pos, bool& ok) const noexcept {
        return static_cast<std::int32_t>(u32(pos, ok));
    }
    std::int16_t i16(std::size_t pos, bool& ok) const noexcept {
        return static_cast<std::int16_t>(u16(pos, ok));
    }
};

inline bool flat_is_scalar(fkind k) noexcept {
    return k == fkind::bool_ || k == fkind::i8 || k == fkind::u8 ||
           k == fkind::i16 || k == fkind::u16 || k == fkind::i32 ||
           k == fkind::u32 || k == fkind::i64 || k == fkind::u64 ||
           k == fkind::f32 || k == fkind::f64;
}

// Inline size of a scalar (0 for non-scalars).
inline std::size_t flat_scalar_size(fkind k) noexcept {
    switch (k) {
        case fkind::bool_:
        case fkind::i8:
        case fkind::u8:
        case fkind::union_type:
            return 1;
        case fkind::i16:
        case fkind::u16:
            return 2;
        case fkind::i32:
        case fkind::u32:
        case fkind::f32:
            return 4;
        case fkind::i64:
        case fkind::u64:
        case fkind::f64:
            return 8;
        default:
            return 0;
    }
}

// The buffer alignment a schema field requires: its own size for scalars, 4
// for every offset-carrying kind (uoffset_t), the struct alignment for stct
// and 1 for the u8 union type byte.
// --- struct layout ----------------------------------------------------------
// FlatBuffers structs are fixed-size inline regions sized like the compiler
// would: each field aligned to its own size (or the max alignment of a nested
// struct), total rounded up to the struct's max alignment.

inline std::size_t struct_align_of(const schema_info* si);
inline std::size_t struct_total(const schema_info* si);

// Byte offset of the i-th struct member from the struct start.
inline std::size_t struct_member_pos(const schema_info* si,
                                     std::size_t which) noexcept {
    std::size_t off = 0;
    for (std::size_t i = 0; i < which; ++i) {
        const field_desc& fd = si->fields[i];
        std::size_t sz;
        std::size_t al;
        if (flat_is_scalar(fd.kind)) {
            sz = flat_scalar_size(fd.kind);
            al = sz;
        } else if (fd.kind == fkind::stct && fd.sub != nullptr) {
            sz = struct_total(fd.sub);
            al = struct_align_of(fd.sub);
        } else {
            return 0;
        }
        off = round_up(off, al) + sz;
    }
    return off;
}

// Max alignment (and, when out_total is non-null, the byte size) of a struct.
inline std::size_t struct_meta(const schema_info* si, std::size_t* out_total) {
    std::size_t off = 0;
    std::size_t max_align = 1;
    for (std::size_t i = 0; i < si->count; ++i) {
        const field_desc& fd = si->fields[i];
        std::size_t sz;
        std::size_t al;
        if (flat_is_scalar(fd.kind)) {
            sz = flat_scalar_size(fd.kind);
            al = sz;
        } else if (fd.kind == fkind::stct && fd.sub != nullptr) {
            sz = struct_total(fd.sub); // needs complete type recursion
            al = struct_align_of(fd.sub);
        } else {
            return 0; // not a valid struct member
        }
        if (al > max_align) {
            max_align = al;
        }
        off = round_up(off, al) + sz;
    }
    off = round_up(off, max_align);
    if (out_total != nullptr) {
        *out_total = off;
    }
    return max_align;
}

inline std::size_t struct_align_of(const schema_info* si) {
    return struct_meta(si, nullptr);
}

inline std::size_t struct_total(const schema_info* si) {
    std::size_t total = 0;
    (void)struct_meta(si, &total);
    return total;
}

inline std::size_t field_alignment(const field_desc& fd) noexcept {
    if (fd.kind == fkind::union_type) {
        return 1; // u8 enum byte
    }
    if (flat_is_scalar(fd.kind)) {
        return flat_scalar_size(fd.kind);
    }
    if (fd.kind == fkind::stct) {
        return struct_align_of(fd.sub);
    }
    return 4; // string / vector / table / union_value
}

// --- scalar codecs ----------------------------------------------------------

// Encode a scalar value into out (little-endian, `is_real` selects the double).
inline void put_scalar(std::string& out, fkind k, std::uint64_t u, double d,
                       bool is_real) noexcept {
    switch (k) {
        case fkind::bool_:
        case fkind::i8:
        case fkind::union_type:
            out.push_back(static_cast<char>(is_real ? static_cast<std::int64_t>(d)
                                                    : static_cast<std::int64_t>(u)));
            return;
        case fkind::u8:
            out.push_back(static_cast<char>(is_real ? static_cast<std::uint64_t>(d)
                                                    : u));
            return;
        case fkind::i16:
            out.push_back(static_cast<char>(u & 0xFFu));
            out.push_back(static_cast<char>((u >> 8) & 0xFFu));
            return;
        case fkind::u16:
            out.push_back(static_cast<char>(u & 0xFFu));
            out.push_back(static_cast<char>((u >> 8) & 0xFFu));
            return;
        case fkind::i32:
        case fkind::u32:
            out.push_back(static_cast<char>(u & 0xFFu));
            out.push_back(static_cast<char>((u >> 8) & 0xFFu));
            out.push_back(static_cast<char>((u >> 16) & 0xFFu));
            out.push_back(static_cast<char>((u >> 24) & 0xFFu));
            return;
        case fkind::i64:
        case fkind::u64:
            for (int i = 0; i < 8; ++i) {
                out.push_back(static_cast<char>((u >> (8 * i)) & 0xFFu));
            }
            return;
        case fkind::f32: {
            const float f = static_cast<float>(d);
            std::uint32_t bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            out.push_back(static_cast<char>(bits & 0xFFu));
            out.push_back(static_cast<char>((bits >> 8) & 0xFFu));
            out.push_back(static_cast<char>((bits >> 16) & 0xFFu));
            out.push_back(static_cast<char>((bits >> 24) & 0xFFu));
            return;
        }
        case fkind::f64: {
            const double f = d;
            std::uint64_t bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            for (int i = 0; i < 8; ++i) {
                out.push_back(static_cast<char>((bits >> (8 * i)) & 0xFFu));
            }
            return;
        }
        default:
            return;
    }
}

// The universal FlatBuffers default for an absent scalar: the declared .fbs
// default if registered, else 0/false/0.0.
inline rtti::field_value scalar_default_value(fkind k,
                                              const field_desc* fd) noexcept {
    if (k == fkind::f32 || k == fkind::f64) {
        return rtti::field_value::of(
            (fd != nullptr && fd->has_default) ? fd->dflt_real : 0.0);
    }
    if (k == fkind::bool_) {
        return rtti::field_value::of(
            (fd != nullptr && fd->has_default) ? (fd->dflt_int != 0) : false);
    }
    return rtti::field_value::of(
        (fd != nullptr && fd->has_default) ? fd->dflt_int : std::int64_t(0));
}

// Decode a scalar at `pos` into the field_value (bool/integer/real).
inline error_code scalar_in_value(rtti::field_value& v, fkind k, const fbuf& b,
                                  std::size_t pos) {
    bool ok = true;
    switch (k) {
        case fkind::bool_:
            v = rtti::field_value::of(b.u8(pos, ok) != 0);
            break;
        case fkind::union_type:
            v = rtti::field_value::of(
                static_cast<std::int64_t>(b.u8(pos, ok)));
            break;
        case fkind::i8:
            v = rtti::field_value::of(
                static_cast<std::int64_t>(static_cast<std::int8_t>(b.u8(pos, ok))));
            break;
        case fkind::u8:
            v = rtti::field_value::of(static_cast<std::int64_t>(b.u8(pos, ok)));
            break;
        case fkind::i16:
            v = rtti::field_value::of(
                static_cast<std::int64_t>(b.i16(pos, ok)));
            break;
        case fkind::u16:
            v = rtti::field_value::of(
                static_cast<std::int64_t>(b.u16(pos, ok)));
            break;
        case fkind::i32:
            v = rtti::field_value::of(
                static_cast<std::int64_t>(b.i32(pos, ok)));
            break;
        case fkind::u32:
            v = rtti::field_value::of(
                static_cast<std::int64_t>(b.u32(pos, ok)));
            break;
        case fkind::i64:
            v = rtti::field_value::of(static_cast<std::int64_t>(b.u64(pos, ok)));
            break;
        case fkind::u64: {
            // uint64 values above INT64_MAX clamp to INT64_MAX on the tree (the
            // value_io integer node is int64); the typed path shares the limit.
            const std::uint64_t u = b.u64(pos, ok);
            const std::uint64_t kMaxInt64 = 0x7FFFFFFFFFFFFFFFull;
            v = rtti::field_value::of(
                u > kMaxInt64 ? static_cast<std::int64_t>(kMaxInt64)
                              : static_cast<std::int64_t>(u));
            break;
        }
        case fkind::f32: {
            std::uint32_t bits = b.u32(pos, ok);
            float f = 0.0f;
            std::memcpy(&f, &bits, sizeof(bits));
            v = rtti::field_value::of(static_cast<double>(f));
            break;
        }
        case fkind::f64: {
            std::uint64_t bits = b.u64(pos, ok);
            double d = 0.0;
            std::memcpy(&d, &bits, sizeof(bits));
            v = rtti::field_value::of(d);
            break;
        }
        default:
            return error_code::kind_mismatch;
    }
    return ok ? error_code::ok : error_code::proto_truncated;
}

// Read a scalar from a tree node into its wire bytes. Integer kinds accept
// boolean/integer nodes; real kinds accept real or integer nodes.
inline error_code scalar_out_value(const rtti::value_io* node, fkind k,
                                   std::string& out) {
    if (node == nullptr) {
        return error_code::kind_mismatch;
    }
    if (k == fkind::f32 || k == fkind::f64) {
        double d = 0.0;
        if (node->type() == rtti::value_io::kind::integer) {
            std::int64_t i = 0;
            if (!node->as_integer(i)) {
                return error_code::kind_mismatch;
            }
            d = static_cast<double>(i);
        } else if (node->type() == rtti::value_io::kind::real) {
            if (!node->as_real(d)) {
                return error_code::kind_mismatch;
            }
        } else {
            return error_code::kind_mismatch;
        }
        put_scalar(out, k, 0, d, true);
        return error_code::ok;
    }
    std::uint64_t u = 0;
    if (k == fkind::bool_) {
        bool b = false;
        if (node->type() == rtti::value_io::kind::boolean) {
            if (!node->as_bool(b)) {
                return error_code::kind_mismatch;
            }
        } else if (node->type() == rtti::value_io::kind::integer) {
            std::int64_t i = 0;
            if (!node->as_integer(i)) {
                return error_code::kind_mismatch;
            }
            b = (i != 0);
        } else {
            return error_code::kind_mismatch;
        }
        u = b ? 1 : 0;
    } else if (node->type() == rtti::value_io::kind::boolean) {
        bool b = false;
        if (!node->as_bool(b)) {
            return error_code::kind_mismatch;
        }
        u = b ? 1 : 0;
    } else if (node->type() == rtti::value_io::kind::integer) {
        std::int64_t i = 0;
        if (!node->as_integer(i)) {
            return error_code::kind_mismatch;
        }
        u = static_cast<std::uint64_t>(i);
    } else {
        return error_code::kind_mismatch;
    }
    put_scalar(out, k, u, 0, false);
    return error_code::ok;
}

// --- forward declarations for the recursive decode/encode walks ------------

struct builder;

inline error_code decode_string(rtti::value_io* node, const fbuf& b,
                                std::size_t str_pos);
inline error_code decode_vector(rtti::value_io* node, const fbuf& b,
                                std::size_t vec_pos, const field_desc& fd,
                                std::size_t depth, std::size_t max_depth);
inline error_code decode_struct_into(rtti::value_io* node, const fbuf& b,
                                     std::size_t pos, const schema_info* si,
                                     std::size_t depth, std::size_t max_depth);
inline bool utf8_valid(rtti::string_view s) noexcept;
inline const field_desc* find_field_by_slot(const schema_info* si,
                                            std::uint16_t slot) noexcept;
inline std::size_t find_index_by_slot(const schema_info* si,
                                      std::uint16_t slot) noexcept;
inline const schema_info* union_variant_for(const field_desc& fd,
                                            std::uint32_t type) noexcept;
inline error_code build_struct(std::string& out, const rtti::value_io* obj,
                               const schema_info* si, std::size_t depth,
                               std::size_t max_depth);
inline error_code build_string(builder& B, rtti::string_view s,
                               std::size_t& start);
inline error_code build_vector(builder& B, const rtti::value_io* arr,
                               const field_desc& fd, std::size_t depth,
                               std::size_t max_depth, std::size_t& start);
inline error_code build_table(builder& B, const rtti::value_io* obj,
                              const schema_info* si, std::size_t depth,
                              std::size_t max_depth, std::size_t& table_pos);

error_code decode_table_into(rtti::value_io* node, const fbuf& b,
                             std::size_t table_pos, const schema_info* si,
                             std::size_t depth, std::size_t max_depth) {
    if (depth > max_depth) {
        return error_code::too_deep;
    }
    if (si == nullptr) {
        return error_code::kind_mismatch;
    }
    if (!b.in_bounds(table_pos, 4)) {
        return error_code::proto_truncated;
    }
    bool ok = true;
    const std::uint32_t soff_u = b.u32(table_pos, ok);
    if (!ok) {
        return error_code::proto_truncated;
    }
    const std::int32_t soff = static_cast<std::int32_t>(soff_u);
    if (soff == 0) {
        return error_code::proto_bad_field; // vtable == table
    }
    // FlatBuffers vtables may live on either side of the table (flatc places a
    // shared vtable after the first writer, so later tables hold negative
    // soffsets). vtable_pos = table_pos - soff must stay inside the buffer.
    const std::int64_t vt = static_cast<std::int64_t>(table_pos) - soff;
    if (vt < 0 || static_cast<std::uint64_t>(vt) >= b.n) {
        return error_code::proto_bad_field;
    }
    const std::size_t vtable_pos = static_cast<std::size_t>(vt);
    if (!b.in_bounds(vtable_pos, 4)) {
        return error_code::proto_truncated;
    }
    const std::uint16_t vtable_size = b.u16(vtable_pos, ok);
    const std::uint16_t table_size = b.u16(vtable_pos + 2, ok);
    if (!ok || vtable_size < 4 || table_size < 4) {
        return error_code::proto_bad_field;
    }
    if (!b.in_bounds(vtable_pos, vtable_size)) {
        return error_code::proto_truncated;
    }
    if (!b.in_bounds(table_pos, table_size)) {
        return error_code::proto_truncated;
    }

    // Per-slot wire info used both by the plain fields and union pairing.
    struct slot_info {
        bool present = false;
        std::uint16_t off = 0;
        std::size_t field_pos = 0;
    };

    node->make_object();
    for (std::size_t f = 0; f < si->count; ++f) {
        const field_desc& fd = si->fields[f];
        slot_info si_w;
        const std::size_t entry = 4 + 2u * fd.slot;
        if (entry + 2 <= vtable_size) {
            si_w.off = b.u16(vtable_pos + entry, ok);
            si_w.present = ok && si_w.off != 0;
            si_w.field_pos = table_pos + si_w.off;
        }

        error_code e = error_code::ok;
        rtti::value_io* child = node->make_node();

        const auto read_slot = [&](const field_desc& tf,
                                   std::uint32_t& out) -> error_code {
            slot_info ts;
            const std::size_t te = 4 + 2u * tf.slot;
            if (te + 2 <= vtable_size) {
                ts.off = b.u16(vtable_pos + te, ok);
                ts.present = ok && ts.off != 0;
                ts.field_pos = table_pos + ts.off;
            }
            if (!ts.present) {
                out = 0;
                return error_code::ok;
            }
            rtti::field_value v;
            const error_code te2 = scalar_in_value(v, fkind::u8, b, ts.field_pos);
            if (te2 != error_code::ok) {
                return te2;
            }
            std::int64_t iv = 0;
            iv = v.as_integer();
            out = static_cast<std::uint32_t>(iv);
            return error_code::ok;
        };

        // Resolve inline region bound for the field's own bytes.
        std::size_t inline_len = 0;
        if (flat_is_scalar(fd.kind) || fd.kind == fkind::union_type) {
            inline_len = flat_scalar_size(fd.kind);
        } else if (fd.kind == fkind::stct && fd.sub != nullptr) {
            inline_len = struct_total(fd.sub);
        } else if (fd.kind == fkind::string || fd.kind == fkind::vector ||
                   fd.kind == fkind::table || fd.kind == fkind::union_value) {
            inline_len = 4;
        }
        const bool align_ok =
            !si_w.present ||
            (inline_len != 0 && (si_w.field_pos % field_alignment(fd)) == 0);
        if (si_w.present && !align_ok) {
            return error_code::proto_bad_field;
        }
        if (si_w.present &&
            (si_w.field_pos < table_pos ||
             si_w.field_pos + inline_len > table_pos + table_size)) {
            return error_code::proto_bad_field;
        }

        switch (fd.kind) {
            case fkind::bool_:
            case fkind::i8:
            case fkind::u8:
            case fkind::i16:
            case fkind::u16:
            case fkind::i32:
            case fkind::u32:
            case fkind::i64:
            case fkind::u64:
            case fkind::f32:
            case fkind::f64:
            case fkind::union_type: {
                rtti::field_value v;
                if (si_w.present) {
                    e = scalar_in_value(v, fd.kind, b, si_w.field_pos);
                } else {
                    v = scalar_default_value(fd.kind, &fd);
                }
                if (e == error_code::ok) {
                    child->make_scalar(v);
                }
                break;
            }
            case fkind::string: {
                if (si_w.present) {
                    const std::uint32_t o = b.u32(si_w.field_pos, ok);
                    if (!ok) {
                        e = error_code::proto_truncated;
                    } else {
                        const std::size_t target = si_w.field_pos + o;
                        if (!b.in_bounds(target, 4)) {
                            e = error_code::proto_truncated;
                        } else {
                            e = decode_string(child, b, target);
                        }
                    }
                } else {
                    child->make_scalar(rtti::field_value::of(rtti::string_view("", 0)));
                }
                break;
            }
            case fkind::vector: {
                if (si_w.present) {
                    const std::uint32_t o = b.u32(si_w.field_pos, ok);
                    if (!ok) {
                        e = error_code::proto_truncated;
                    } else {
                        const std::size_t target = si_w.field_pos + o;
                        if (!b.in_bounds(target, 4)) {
                            e = error_code::proto_truncated;
                        } else {
                            e = decode_vector(child, b, target, fd, depth, max_depth);
                        }
                    }
                } else {
                    child->make_array();
                }
                break;
            }
            case fkind::table: {
                if (si_w.present && fd.sub != nullptr) {
                    const std::uint32_t o = b.u32(si_w.field_pos, ok);
                    if (!ok) {
                        e = error_code::proto_truncated;
                    } else {
                        const std::size_t target = si_w.field_pos + o;
                        if (!b.in_bounds(target, 4)) {
                            e = error_code::proto_truncated;
                        } else {
                            e = decode_table_into(child, b, target, fd.sub,
                                                  depth + 1, max_depth);
                        }
                    }
                } else {
                    child->make_object();
                }
                break;
            }
            case fkind::stct: {
                if (si_w.present && fd.sub != nullptr) {
                    e = decode_struct_into(child, b, si_w.field_pos, fd.sub,
                                           depth + 1, max_depth);
                } else {
                    child->make_object();
                }
                break;
            }
            case fkind::union_value: {
                // The paired union_type field selects the variant schema.
                std::uint32_t tv = 0;
                const field_desc* tf =
                    find_field_by_slot(si, fd.union_type_slot);
                if (tf != nullptr) {
                    e = read_slot(*tf, tv);
                    if (e != error_code::ok) {
                        break;
                    }
                }
                const schema_info* sub = union_variant_for(fd, tv);
                if (!si_w.present || (tf != nullptr && tv == 0)) {
                    child->make_scalar(rtti::field_value{});
                } else if (sub != nullptr) {
                    const std::uint32_t o = b.u32(si_w.field_pos, ok);
                    if (!ok) {
                        e = error_code::proto_truncated;
                    } else {
                        const std::size_t target = si_w.field_pos + o;
                        if (!b.in_bounds(target, 4)) {
                            e = error_code::proto_truncated;
                        } else {
                            e = decode_table_into(child, b, target, sub,
                                                  depth + 1, max_depth);
                        }
                    }
                } else {
                    // Unregistered variant: surface the raw value bytes opaque.
                    const std::uint32_t o = b.u32(si_w.field_pos, ok);
                    if (!ok) {
                        e = error_code::proto_truncated;
                    } else if (static_cast<std::uint64_t>(o) >
                               b.n - si_w.field_pos) {
                        e = error_code::proto_truncated;
                    } else {
                        child->make_scalar(rtti::field_value::of(rtti::string_view(
                            reinterpret_cast<const char*>(b.p + si_w.field_pos + o),
                            b.n - si_w.field_pos - o)));
                    }
                }
                break;
            }
            default:
                e = error_code::kind_mismatch;
                break;
        }
        if (e != error_code::ok) {
            return e;
        }
        node->set_key(rtti::string_view(fd.name), child);
    }
    return error_code::ok;
}
const field_desc* find_field_by_slot(const schema_info* si,
                                     std::uint16_t slot) noexcept;

// Look up the union variant schema whose wire enum value equals `type`.
const schema_info* union_variant_for(const field_desc& fd,
                                     std::uint32_t type) noexcept;

// --- decode: strings / vectors / structs / tables ---------------------------

inline error_code decode_string(rtti::value_io* node, const fbuf& b,
                                std::size_t str_pos) {
    bool ok = true;
    const std::uint32_t len = b.u32(str_pos, ok);
    if (!ok) {
        return error_code::proto_truncated;
    }
    // The byte data is [str_pos+4, +len) followed by the mandatory NUL.
    const std::size_t data = str_pos + 4;
    if (!b.in_bounds(data, static_cast<std::size_t>(len) + 1)) {
        return error_code::proto_truncated;
    }
    if (b.p[data + len] != 0) {
        return error_code::proto_bad_field;
    }
    node->make_scalar(rtti::field_value::of(
        rtti::string_view(reinterpret_cast<const char*>(b.p + data), len)));
    if (len > 0) {
        // Strings are UTF-8 in FlatBuffers; reject invalid UTF-8 on the read
        // path so the tree never carries malformed text.
        rtti::string_view sv;
        if (!node->as_string(sv)) {
            return error_code::kind_mismatch;
        }
        if (!utf8_valid(sv)) {
            return error_code::invalid_utf8;
        }
    }
    return error_code::ok;
}

inline error_code decode_vector(rtti::value_io* node, const fbuf& b,
                                std::size_t vec_pos, const field_desc& fd,
                                std::size_t depth, std::size_t max_depth) {
    bool ok = true;
    const std::uint32_t count = b.u32(vec_pos, ok);
    if (!ok) {
        return error_code::proto_truncated;
    }
    node->make_array();
    if (count == 0) {
        return error_code::ok;
    }
    if (flat_is_scalar(fd.elem)) {
        const std::size_t esz = flat_scalar_size(fd.elem);
        const std::size_t start = vec_pos + 4;
        if ((start % esz) != 0) {
            return error_code::proto_bad_field;
        }
        if (!b.in_bounds(start, static_cast<std::size_t>(count) * esz)) {
            return error_code::proto_truncated;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            rtti::value_io* el = node->make_node();
            rtti::field_value v;
            const error_code e =
                scalar_in_value(v, fd.elem, b, start + static_cast<std::size_t>(i) * esz);
            if (e != error_code::ok) {
                return e;
            }
            el->make_scalar(v);
            node->push_back(el);
        }
        return error_code::ok;
    }
    if (fd.elem == fkind::string || fd.elem == fkind::table) {
        const std::size_t start = vec_pos + 4;
        if ((start % 4) != 0) {
            return error_code::proto_bad_field;
        }
        if (!b.in_bounds(start, static_cast<std::size_t>(count) * 4)) {
            return error_code::proto_truncated;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t epos = start + static_cast<std::size_t>(i) * 4;
            const std::uint32_t o = b.u32(epos, ok);
            if (!ok) {
                return error_code::proto_truncated;
            }
            const std::size_t target = epos + o;
            if (!b.in_bounds(target, 4)) {
                return error_code::proto_truncated;
            }
            rtti::value_io* el = node->make_node();
            error_code e = error_code::ok;
            if (fd.elem == fkind::string) {
                e = decode_string(el, b, target);
            } else {
                if (fd.sub == nullptr) {
                    return error_code::kind_mismatch;
                }
                e = decode_table_into(el, b, target, fd.sub, depth + 1,
                                      max_depth);
            }
            if (e != error_code::ok) {
                return e;
            }
            node->push_back(el);
        }
        return error_code::ok;
    }
    if (fd.elem == fkind::stct) {
        if (fd.sub == nullptr) {
            return error_code::kind_mismatch;
        }
        const std::size_t esz = struct_total(fd.sub);
        const std::size_t start = vec_pos + 4;
        if ((start % struct_align_of(fd.sub)) != 0) {
            return error_code::proto_bad_field;
        }
        if (!b.in_bounds(start, static_cast<std::size_t>(count) * esz)) {
            return error_code::proto_truncated;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            rtti::value_io* el = node->make_node();
            const error_code e =
                decode_struct_into(el, b, start + static_cast<std::size_t>(i) * esz,
                                   fd.sub, depth + 1, max_depth);
            if (e != error_code::ok) {
                return e;
            }
            node->push_back(el);
        }
        return error_code::ok;
    }
    return error_code::kind_mismatch;
}

inline error_code decode_struct_into(rtti::value_io* node, const fbuf& b,
                                     std::size_t pos, const schema_info* si,
                                     std::size_t depth,
                                     std::size_t max_depth) {
    if (depth > max_depth) {
        return error_code::too_deep;
    }
    const std::size_t total = struct_total(si);
    if (total == 0 || !b.in_bounds(pos, total)) {
        return error_code::proto_truncated;
    }
    node->make_object();
    for (std::size_t i = 0; i < si->count; ++i) {
        const field_desc& fd = si->fields[i];
        const std::size_t mpos = pos + struct_member_pos(si, i);
        rtti::value_io* child = node->make_node();
        if (flat_is_scalar(fd.kind)) {
            rtti::field_value v;
            const error_code e = scalar_in_value(v, fd.kind, b, mpos);
            if (e != error_code::ok) {
                return e;
            }
            child->make_scalar(v);
        } else if (fd.kind == fkind::stct && fd.sub != nullptr) {
            const error_code e =
                decode_struct_into(child, b, mpos, fd.sub, depth + 1, max_depth);
            if (e != error_code::ok) {
                return e;
            }
        } else {
            return error_code::kind_mismatch;
        }
        node->set_key(rtti::string_view(fd.name), child);
    }
    return error_code::ok;
}

// Local UTF-8 validator (strings in FlatBuffers are UTF-8 text).
inline bool utf8_valid(rtti::string_view s) noexcept {
    const unsigned char* p =
        reinterpret_cast<const unsigned char*>(s.data());
    const unsigned char* const end = p + s.size();
    while (p < end) {
        const unsigned char c = *p;
        std::size_t need = 0;
        std::uint32_t cp = 0;
        if (c < 0x80) {
            ++p;
            continue;
        } else if ((c & 0xE0u) == 0xC0u) {
            need = 1;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0u) == 0xE0u) {
            need = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8u) == 0xF0u) {
            need = 3;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (static_cast<std::size_t>(end - p) < need + 1) {
            return false;
        }
        for (std::size_t i = 1; i <= need; ++i) {
            const unsigned char cc = p[i];
            if ((cc & 0xC0u) != 0x80u) {
                return false;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        // Overlong, surrogate and out-of-range encodings are invalid.
        if ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) ||
            (need == 3 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        p += need + 1;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------

// Forward-growing, alignment-padded buffer writer. Tables record their field
// offsets before any out-of-line object is built and patch the placeholders
// afterwards, so the output is spec-equivalent to flatc (same accessors read
// the same values) though not byte-identical (no vtable dedup).
struct builder {
    std::string out;

    // Reserve the header (root uoffset + optional file_identifier) and emit
    // the identifier bytes when the declaring schema has a well-formed one.
    void begin(const schema_info* si) {
        out.clear();
        const bool has_id = si != nullptr && is_id4(si->identifier);
        out.assign(has_id ? 8u : 4u, '\0');
        if (has_id) {
            std::memcpy(&out[4], si->identifier, 4);
        }
    }
    std::size_t pos() const { return out.size(); }
    void pad(std::size_t align) {
        const std::size_t r = out.size() % align;
        if (r) {
            out.append(align - r, '\0');
        }
    }
    void put_u16(std::size_t at, std::uint16_t v) {
        if (at + 2 > out.size()) {
            out.resize(at + 2, '\0'); // grow when writing past the end
        }
        out[at] = static_cast<char>(v & 0xFFu);
        out[at + 1] = static_cast<char>((v >> 8) & 0xFFu);
    }
    void put_u32(std::size_t at, std::uint32_t v) {
        if (at + 4 > out.size()) {
            out.resize(at + 4, '\0'); // grow when writing past the end
        }
        for (int i = 0; i < 4; ++i) {
            out[at + i] = static_cast<char>((v >> (8 * i)) & 0xFFu);
        }
    }
};

inline const field_desc* find_field_by_slot(const schema_info* si,
                                            std::uint16_t slot) noexcept {
    if (si == nullptr) {
        return nullptr;
    }
    for (std::size_t i = 0; i < si->count; ++i) {
        if (si->fields[i].slot == slot) {
            return &si->fields[i];
        }
    }
    return nullptr;
}

// Index of the schema field occupying `slot` (or si->count when absent).
inline std::size_t find_index_by_slot(const schema_info* si,
                                      std::uint16_t slot) noexcept {
    if (si == nullptr) {
        return 0;
    }
    for (std::size_t i = 0; i < si->count; ++i) {
        if (si->fields[i].slot == slot) {
            return i;
        }
    }
    return si->count;
}

// Build one inline struct into `out` (local padding, no external alignment).
inline error_code build_struct(std::string& out, const rtti::value_io* obj,
                               const schema_info* si, std::size_t depth,
                               std::size_t max_depth) {
    if (depth > max_depth) {
        return error_code::too_deep;
    }
    if (si == nullptr || obj == nullptr ||
        obj->type() != rtti::value_io::kind::object) {
        return error_code::kind_mismatch;
    }
    for (std::size_t i = 0; i < si->count; ++i) {
        const field_desc& fd = si->fields[i];
        rtti::value_io* child = nullptr;
        if (!obj->get_key(rtti::string_view(fd.name), child) || child == nullptr) {
            return error_code::kind_mismatch;
        }
        if (flat_is_scalar(fd.kind)) {
            const std::size_t al = flat_scalar_size(fd.kind);
            const std::size_t r = out.size() % al;
            if (r) {
                out.append(al - r, '\0');
            }
            const error_code e = scalar_out_value(child, fd.kind, out);
            if (e != error_code::ok) {
                return e;
            }
        } else if (fd.kind == fkind::stct && fd.sub != nullptr) {
            const std::size_t al = struct_align_of(fd.sub);
            const std::size_t r = out.size() % al;
            if (r) {
                out.append(al - r, '\0');
            }
            const error_code e =
                build_struct(out, child, fd.sub, depth + 1, max_depth);
            if (e != error_code::ok) {
                return e;
            }
        } else {
            return error_code::kind_mismatch;
        }
    }
    // Size to the struct's own max alignment (matches the reader's layout).
    const std::size_t a = struct_align_of(si);
    const std::size_t r = out.size() % a;
    if (r) {
        out.append(a - r, '\0');
    }
    return error_code::ok;
}

inline error_code build_string(builder& B, rtti::string_view s,
                               std::size_t& start) {
    B.pad(4);
    start = B.pos();
    B.put_u32(start, static_cast<std::uint32_t>(s.size()));
    B.out.append(s.data(), s.size());
    B.out.push_back('\0');
    return error_code::ok;
}

// Record a uoffset at `at` pointing `target` bytes beyond it.
inline error_code patch_offset(builder& B, std::size_t at,
                               std::size_t target) {
    if (target < at || target - at > 0xFFFFFFFFull) {
        return error_code::overflow;
    }
    B.put_u32(at, static_cast<std::uint32_t>(target - at));
    return error_code::ok;
}

inline error_code build_vector(builder& B, const rtti::value_io* arr,
                               const field_desc& fd, std::size_t depth,
                               std::size_t max_depth, std::size_t& start) {
    if (arr == nullptr || arr->type() != rtti::value_io::kind::array) {
        return error_code::kind_mismatch;
    }
    const std::size_t count = arr->size();
    if (count > 0xFFFFFFFFull) {
        return error_code::overflow;
    }
    // The element data starts right after the 4-byte length prefix, with no
    // gap, so the length-prefix position must make `length + 4` aligned to the
    // element alignment (the uoffset target itself needs 4-byte alignment).
    std::size_t data_align = 4; // uoffset elements (string/table)
    if (flat_is_scalar(fd.elem)) {
        data_align = flat_scalar_size(fd.elem);
        if (data_align == 0) {
            return error_code::kind_mismatch;
        }
    } else if (fd.elem == fkind::stct && fd.sub != nullptr) {
        data_align = struct_align_of(fd.sub);
    }
    std::size_t count_pos = round_up(B.pos(), 4);
    if (data_align > 4) {
        while ((count_pos + 4) % data_align != 0) {
            count_pos = round_up(count_pos + 1, 4);
        }
    }
    if (count_pos > B.pos()) {
        B.out.append(count_pos - B.pos(), '\0');
    }
    start = B.pos();
    B.put_u32(start, static_cast<std::uint32_t>(count));
    if (count == 0) {
        return error_code::ok;
    }
    if (flat_is_scalar(fd.elem)) {
        for (std::size_t i = 0; i < count; ++i) {
            const rtti::value_io* el = arr->at(i);
            if (el == nullptr) {
                return error_code::kind_mismatch;
            }
            const error_code e = scalar_out_value(el, fd.elem, B.out);
            if (e != error_code::ok) {
                return e;
            }
        }
        return error_code::ok;
    }
    if (fd.elem == fkind::string || fd.elem == fkind::table) {
        std::vector<std::size_t> eps;
        eps.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            B.pad(4);
            eps.push_back(B.pos());
            B.out.append(4, '\0');
        }
        for (std::size_t i = 0; i < count; ++i) {
            const rtti::value_io* el = arr->at(i);
            if (el == nullptr) {
                return error_code::kind_mismatch;
            }
            std::size_t target = 0;
            error_code e = error_code::ok;
            if (fd.elem == fkind::string) {
                rtti::string_view sv;
                if (!el->as_string(sv)) {
                    return error_code::kind_mismatch;
                }
                e = build_string(B, sv, target);
            } else {
                if (fd.sub == nullptr) {
                    return error_code::kind_mismatch;
                }
                e = build_table(B, el, fd.sub, depth + 1, max_depth, target);
            }
            if (e != error_code::ok) {
                return e;
            }
            e = patch_offset(B, eps[i], target);
            if (e != error_code::ok) {
                return e;
            }
        }
        return error_code::ok;
    }
    if (fd.elem == fkind::stct) {
        if (fd.sub == nullptr) {
            return error_code::kind_mismatch;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const rtti::value_io* el = arr->at(i);
            if (el == nullptr) {
                return error_code::kind_mismatch;
            }
            const error_code e =
                build_struct(B.out, el, fd.sub, depth + 1, max_depth);
            if (e != error_code::ok) {
                return e;
            }
        }
        return error_code::ok;
    }
    return error_code::kind_mismatch;
}

// Look up the union variant schema whose wire enum value equals `type`.
inline const schema_info* union_variant_for(const field_desc& fd,
                                            std::uint32_t type) noexcept {
    for (std::size_t i = 0; i < fd.variant_count; ++i) {
        if (fd.variants[i].value == type) {
            return fd.variants[i].schema;
        }
    }
    return nullptr;
}

// Build one table body (vtable + inline fields + out-of-line targets).
inline error_code build_table(builder& B, const rtti::value_io* obj,
                              const schema_info* si, std::size_t depth,
                              std::size_t max_depth, std::size_t& table_pos) {
    if (depth > max_depth) {
        return error_code::too_deep;
    }
    if (si == nullptr || obj == nullptr ||
        obj->type() != rtti::value_io::kind::object) {
        return error_code::kind_mismatch;
    }
    const std::size_t n = si->count;
    std::vector<bool> present(n, false);
    std::vector<std::uint32_t> types(n, 0);
    std::vector<std::size_t> fpos(n, 0);
    std::vector<std::uint16_t> offs(n, 0);
    std::vector<const rtti::value_io*> nodes(n, nullptr);

    // Pass 1: resolve the per-field tree nodes and union wire types.
    for (std::size_t f = 0; f < n; ++f) {
        const field_desc& fd = si->fields[f];
        rtti::value_io* child = nullptr;
        if (obj->get_key(rtti::string_view(fd.name), child) && child != nullptr &&
            child->type() != rtti::value_io::kind::null) {
            nodes[f] = child;
            if (fd.kind == fkind::union_type) {
                if (child->type() == rtti::value_io::kind::integer) {
                    std::int64_t iv = 0;
                    if (child->as_integer(iv)) {
                        types[f] = static_cast<std::uint32_t>(iv);
                    }
                } else if (child->type() == rtti::value_io::kind::boolean) {
                    bool bv = false;
                    if (child->as_bool(bv)) {
                        types[f] = bv ? 1u : 0u;
                    }
                }
            }
        }
    }
    // Pass 2: decide which slots are emitted.
    for (std::size_t f = 0; f < n; ++f) {
        const field_desc& fd = si->fields[f];
        const rtti::value_io* child = nodes[f];
        if (fd.kind == fkind::union_type) {
            present[f] = (child != nullptr && types[f] != 0);
        } else if (fd.kind == fkind::union_value) {
            std::uint32_t tv = 0;
            const field_desc* tf =
                (fd.union_type_slot != fd.slot)
                    ? find_field_by_slot(si, fd.union_type_slot)
                    : nullptr;
            if (tf != nullptr) {
                tv = types[find_index_by_slot(si, tf->slot)];
            }
            present[f] = (tv != 0 && child != nullptr &&
                          child->type() == rtti::value_io::kind::object &&
                          union_variant_for(fd, tv) != nullptr);
        } else {
            present[f] = (child != nullptr);
        }
    }

    std::uint16_t max_present_slot = 0;
    bool any = false;
    for (std::size_t f = 0; f < n; ++f) {
        if (present[f]) {
            any = true;
            if (si->fields[f].slot > max_present_slot) {
                max_present_slot = si->fields[f].slot;
            }
        }
    }
    const std::uint16_t vtable_slots = any
                                           ? static_cast<std::uint16_t>(max_present_slot + 1)
                                           : 0;
    const std::size_t vtable_size = 4 + 2u * vtable_slots;

    B.pad(2);
    const std::size_t vtable_pos = B.pos();
    B.out.append(vtable_size, '\0');

    // Align the table start to the max present field alignment.
    std::size_t table_align = 4;
    for (std::size_t f = 0; f < n; ++f) {
        if (present[f]) {
            const std::size_t al = field_alignment(si->fields[f]);
            if (al > table_align) {
                table_align = al;
            }
        }
    }
    B.pad(table_align);
    table_pos = B.pos();
    B.out.append(4, '\0'); // table prefix: soffset back to the vtable

    for (std::size_t f = 0; f < n; ++f) {
        if (!present[f]) {
            continue;
        }
        const field_desc& fd = si->fields[f];
        B.pad(field_alignment(fd));
        const std::size_t body_off = B.pos() - table_pos;
        if (body_off > 0xFFFF) {
            return error_code::overflow;
        }
        offs[f] = static_cast<std::uint16_t>(body_off);
        fpos[f] = B.pos();
        if (flat_is_scalar(fd.kind) || fd.kind == fkind::union_type) {
            const error_code e = scalar_out_value(nodes[f], fd.kind, B.out);
            if (e != error_code::ok) {
                return e;
            }
        } else if (fd.kind == fkind::stct) {
            if (fd.sub == nullptr) {
                return error_code::kind_mismatch;
            }
            const error_code e =
                build_struct(B.out, nodes[f], fd.sub, depth + 1, max_depth);
            if (e != error_code::ok) {
                return e;
            }
        } else {
            B.out.append(4, '\0'); // out-of-line uoffset placeholder
        }
    }

    const std::size_t inline_size = B.pos() - table_pos;
    if (inline_size > 0xFFFF) {
        return error_code::overflow;
    }

    // Build the out-of-line targets and patch every placeholder.
    for (std::size_t f = 0; f < n; ++f) {
        if (!present[f]) {
            continue;
        }
        const field_desc& fd = si->fields[f];
        std::size_t target = 0;
        error_code e = error_code::ok;
        if (fd.kind == fkind::string) {
            rtti::string_view sv;
            if (!nodes[f]->as_string(sv)) {
                return error_code::kind_mismatch;
            }
            e = build_string(B, sv, target);
        } else if (fd.kind == fkind::vector) {
            e = build_vector(B, nodes[f], fd, depth + 1, max_depth, target);
        } else if (fd.kind == fkind::table) {
            if (fd.sub == nullptr) {
                return error_code::kind_mismatch;
            }
            e = build_table(B, nodes[f], fd.sub, depth + 1, max_depth, target);
        } else if (fd.kind == fkind::union_value) {
            std::uint32_t tv = 0;
            const field_desc* tf =
                (fd.union_type_slot != fd.slot)
                    ? find_field_by_slot(si, fd.union_type_slot)
                    : nullptr;
            if (tf != nullptr) {
                tv = types[find_index_by_slot(si, tf->slot)];
            }
            const schema_info* sub = union_variant_for(fd, tv);
            if (sub == nullptr) {
                return error_code::kind_mismatch;
            }
            e = build_table(B, nodes[f], sub, depth + 1, max_depth, target);
        } else {
            continue;
        }
        if (e != error_code::ok) {
            return e;
        }
        e = patch_offset(B, fpos[f], target);
        if (e != error_code::ok) {
            return e;
        }
    }

    // Vtable: size, table size, then one u16 per field slot (0 = absent).
    B.put_u16(vtable_pos, static_cast<std::uint16_t>(vtable_size));
    B.put_u16(vtable_pos + 2, static_cast<std::uint16_t>(inline_size));
    for (std::uint16_t s = 0; s < vtable_slots; ++s) {
        std::uint16_t entry = 0;
        for (std::size_t f = 0; f < n; ++f) {
            if (present[f] && si->fields[f].slot == s) {
                entry = offs[f];
                break;
            }
        }
        B.put_u16(vtable_pos + 4 + 2u * s, entry);
    }
    // Table prefix: soffset back to its vtable.
    const std::size_t soff = table_pos - vtable_pos;
    if (soff > 0x7FFFFFFFull) {
        return error_code::overflow;
    }
    B.put_u32(table_pos, static_cast<std::uint32_t>(soff));
    return error_code::ok;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool verify_file_identifier(rtti::string_view src, const char* identifier) {
    if (!is_id4(identifier)) {
        return true; // no (or malformed) identifier declares nothing to check
    }
    return src.size() >= 8 &&
           std::memcmp(src.data() + 4, identifier, 4) == 0;
}

error_code wire_to_tree(rtti::string_view src, const schema_info* si,
                        document& doc, const parse_config& cfg) {
    if (si == nullptr) {
        return error_code::kind_mismatch;
    }
    // Validate the optional file identifier before any table read.
    if (is_id4(si->identifier)) {
        if (!verify_file_identifier(src, si->identifier)) {
            return error_code::invalid_root;
        }
    }
    if (src.size() < 4) {
        return error_code::invalid_root;
    }
    bool ok = true;
    const std::uint32_t root_off = detail::fbuf(src).u32(0, ok);
    if (!ok || root_off == 0 || (root_off & 0x80000000u) != 0u) {
        return error_code::proto_bad_field;
    }
    const std::size_t root_pos = root_off;
    if (!detail::fbuf(src).in_bounds(root_pos, 4)) {
        return error_code::proto_truncated;
    }
    doc.make_object();
    return detail::decode_table_into(&doc, detail::fbuf(src), root_pos, si, 0,
                                     cfg.max_depth);
}

error_code tree_to_wire(const rtti::value_io* root, const schema_info* si,
                        std::string& out, const parse_config& cfg) {
    if (si == nullptr || root == nullptr ||
        root->type() != rtti::value_io::kind::object) {
        return error_code::kind_mismatch;
    }
    detail::builder B;
    B.begin(si);
    std::size_t table_pos = 0;
    const error_code e =
        detail::build_table(B, root, si, 0, cfg.max_depth, table_pos);
    if (e != error_code::ok) {
        return e;
    }
    if (table_pos > 0xFFFFFFFFull) {
        return error_code::overflow;
    }
    B.put_u32(0, static_cast<std::uint32_t>(table_pos));
    out = std::move(B.out);
    return error_code::ok;
}

result<document> parse(rtti::string_view src, const schema_info* si,
                       const parse_config& cfg) {
    result<document> r;
    document doc;
    const error_code e = wire_to_tree(src, si, doc, cfg);
    if (e != error_code::ok) {
        r.err.code = e;
        r.err.src = src.data();
        r.err.src_len = src.size();
        return r;
    }
    r.value = std::move(doc);
    return r;
}

} // namespace flat
} // namespace rtti