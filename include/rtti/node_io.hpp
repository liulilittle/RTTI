#pragma once
// Node-level conversion: locate / node_as / to_node (spec 4.3). Format
// independent bridges over the value_io DOM interface shared by every format
// adapter (JSON/XML/INI/TOML/proto and the flatbuffers doc when enabled).
// English comments only; C++17; no third-party dependencies.
//
//   * rtti::locate(root, "user.addresses[1].city") yields the const or mutable
//     node at a dot-separated key + "[index]" path. Missing keys, out-of-range
//     indexes and malformed paths report an error_code; locate never crashes
//     (spec section 0).
//   * rtti::node_as<T>(node) converts a located node to a modern C++ value
//     through the existing to_value dispatch (scalars, containers, reflected
//     structs).
//   * rtti::to_node(target, value) writes / replaces the target node's subtree
//     from a C++ value through from_value, so the owning document serializes
//     with the updated contents.
//
// locate is non-template and implemented in src/reflect.cpp; node_as / to_node
// are thin header templates over the convert layer.

#include <rtti/convert.hpp>
#include <rtti/error.hpp>

namespace rtti {

// Const-tree path navigation. Every format document derives from value_io, so
// this free function is directly usable on json/xml/ini/toml/proto documents.
//
// Path grammar: step ('.' step)*, where a step is a key optionally followed by
// one or more "[index]" groups ("a.b[2].c") or one or more leading root
// indexes ("[0][1].x"). An empty path selects nothing and returns an error.
// Error mapping: a missing key or an out-of-range index reports
// no_such_field; navigating a key/index into a non-object / non-array and any
// malformed path shape (empty segment, stray dot, an index step after '.',
// malformed brackets) reports kind_mismatch. The returned pointer borrows the
// tree and stays valid while the tree lives.
result<const value_io*> locate(const value_io& root, string_view path) noexcept;

// Mutable overload: same navigation, returns mutable node pointers (usable as
// the target of to_node).
result<value_io*> locate(value_io& root, string_view path) noexcept;

// Convert the node `node` into a T through the convert layer. `node` may be
// null (reports no_such_field); a kind mismatch reports the converter's code.
// T must be default-constructible (the result payload is value-initialized).
template <typename T>
result<T> node_as(const value_io* node) {
    result<T> r;
    r.err.code = from_value(node, r.value);
    return r;
}

// Replace the subtree under `target` with `v` (through from_value). The
// adapters' make_* write side resets the target's previous children, so a
// located scalar / object / array node is fully replaced and the owning
// document serializes the new value. Returns the converter's error code.
template <typename T>
error_code to_node(value_io& target, const T& v) {
    return to_value(&target, v);
}

} // namespace rtti