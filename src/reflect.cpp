// Reflection-core translation unit for the RTTI static library.
// Node-level path navigation (rtti::locate) lives here: it is pure runtime
// dispatch over the value_io interface, so it needs no templates and belongs
// in the compiled core. node_as / to_node are thin header templates over the
// convert layer (see node_io.hpp).

#include <cstdint>
#include <limits>
#include <vector>

#include <rtti/node_io.hpp>

namespace rtti {
namespace {

// One step of a locate path: an object key or an array index.
struct locate_step {
    bool is_index = false;
    string_view key;
    std::size_t index = 0;
};

// Guarded decimal index parse. On entry path[i] == '['; on success `i` is
// advanced past the closing ']' and `out` receives the index. A malformed
// group reports kind_mismatch; a numeric overflow reports no_such_field (the
// index cannot address any real element).
error_code parse_index_at(string_view path, std::size_t& i, std::size_t& out) {
    const std::size_t n = path.size();
    std::size_t j = i + 1;
    if (j >= n || path[j] < '0' || path[j] > '9') {
        return error_code::kind_mismatch; // "[]" or "[x..."
    }
    std::size_t idx = 0;
    for (; j < n && path[j] >= '0' && path[j] <= '9'; ++j) {
        const std::size_t d = static_cast<std::size_t>(path[j] - '0');
        const std::size_t maxv = (std::numeric_limits<std::size_t>::max)();
        if (idx > (maxv - d) / 10) {
            return error_code::no_such_field; // index too large to represent
        }
        idx = idx * 10 + d;
    }
    if (j >= n || path[j] != ']') {
        return error_code::kind_mismatch; // unclosed or malformed group
    }
    out = idx;
    i = j + 1;
    return error_code::ok;
}

// Split `path` into navigation steps under the grammar
//   path := step ('.' step)*     first_step := key idx* | idx+
//   later_step := key idx*
//   key  := any run without '.' or '['      idx := '[' digits ']'
// An empty path (no steps) yields no_such_field; every malformed shape
// (leading / trailing / double dot, empty key, an index step after '.', junk
// inside brackets) yields kind_mismatch.
error_code tokenize_locate_path(string_view path, std::vector<locate_step>& steps) {
    steps.clear();
    if (path.empty()) {
        return error_code::no_such_field;
    }
    const std::size_t n = path.size();
    std::size_t i = 0;
    bool first = true;
    while (i < n) {
        if (path[i] == '.') {
            return error_code::kind_mismatch; // empty step at this position
        }
        if (path[i] == '[') {
            if (!first) {
                return error_code::kind_mismatch; // index step after a '.'
            }
            while (i < n && path[i] == '[') {
                locate_step st;
                st.is_index = true;
                const error_code ec = parse_index_at(path, i, st.index);
                if (ec != error_code::ok) {
                    return ec;
                }
                steps.push_back(st);
            }
        } else {
            locate_step st;
            st.is_index = false;
            std::size_t k = i;
            while (k < n && path[k] != '.' && path[k] != '[') {
                ++k;
            }
            st.key = path.substr(i, k - i);
            steps.push_back(st);
            i = k;
            while (i < n && path[i] == '[') {
                locate_step g;
                g.is_index = true;
                const error_code ec = parse_index_at(path, i, g.index);
                if (ec != error_code::ok) {
                    return ec;
                }
                steps.push_back(g);
            }
        }
        first = false;
        if (i < n) {
            if (path[i] != '.') {
                return error_code::kind_mismatch; // junk between steps
            }
            ++i;
            if (i >= n) {
                return error_code::kind_mismatch; // trailing dot
            }
        }
    }
    return error_code::ok;
}

// Single const / non-const navigation engine. `Node` is `value_io` or
// `const value_io`. Returns no_such_field when a key is absent or an index is
// out of range; kind_mismatch when a key/index is applied to a scalar or to an
// object/array of the wrong kind.
template <typename Node>
result<Node*> locate_impl(Node* root, string_view path) {
    result<Node*> r;
    if (root == nullptr) {
        r.err.code = error_code::no_such_field;
        return r;
    }
    std::vector<locate_step> steps;
    const error_code ec = tokenize_locate_path(path, steps);
    if (ec != error_code::ok) {
        r.err.code = ec;
        return r;
    }
    Node* cur = root;
    for (const locate_step& s : steps) {
        if (s.is_index) {
            if (cur->type() != value_io::kind::array) {
                r.err.code = error_code::kind_mismatch;
                return r;
            }
            value_io* next = cur->at(s.index);
            if (next == nullptr) {
                r.err.code = error_code::no_such_field;
                return r;
            }
            cur = static_cast<Node*>(next);
        } else {
            if (cur->type() != value_io::kind::object) {
                r.err.code = error_code::kind_mismatch;
                return r;
            }
            value_io* next = nullptr;
            if (!cur->get_key(s.key, next) || next == nullptr) {
                r.err.code = error_code::no_such_field;
                return r;
            }
            cur = static_cast<Node*>(next);
        }
    }
    r.value = cur;
    return r;
}

} // namespace

result<value_io*> locate(value_io& root, string_view path) noexcept {
    return locate_impl<value_io>(&root, path);
}

result<const value_io*> locate(const value_io& root, string_view path) noexcept {
    return locate_impl<const value_io>(&root, path);
}

} // namespace rtti