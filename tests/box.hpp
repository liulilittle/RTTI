// Shared in-memory value_io test double for the test suite.
// Implements rtti::value_io over its own tree; used by convert/recursion and,
// later, format adapter tests. English comments only.

#pragma once

#include <rtti/convert.hpp>
#include <map>
#include <string>
#include <vector>

namespace rtti_test_lib {

struct box : rtti::value_io {
    using child_list = std::vector<box*>;
    using child_map = std::map<std::string, box*>;

    rtti::field_value scalar;
    child_list arr;
    child_map obj;
    std::string owned;  // owns string payload so views stay valid after the source is destroyed
    bool is_object = false;
    bool is_array = false;

    box() = default;
    ~box() override { clear_owned(); }

    // The value_io make_* contract (mirrored by the arena adapters, e.g.
    // json.cpp) is that a structural write resets the node's current content:
    // reusing a scratch node (element_to / container for_each) must release
    // the children it already owns, never silently drop their pointers.
    void clear_owned() noexcept {
        for (box* c : arr) {
            delete c;
        }
        for (auto& kv : obj) {
            delete kv.second;
        }
        arr.clear();
        obj.clear();
    }

    rtti::value_io::kind type() const noexcept override {
        if (is_object) {
            return rtti::value_io::kind::object;
        }
        if (is_array) {
            return rtti::value_io::kind::array;
        }
        switch (scalar.type()) {
            case rtti::field_value::kind::boolean:
                return rtti::value_io::kind::boolean;
            case rtti::field_value::kind::integer:
                return rtti::value_io::kind::integer;
            case rtti::field_value::kind::real:
                return rtti::value_io::kind::real;
            case rtti::field_value::kind::string:
                return rtti::value_io::kind::string;
            default:
                return rtti::value_io::kind::null;
        }
    }
    bool as_bool(bool& v) const noexcept override {
        if (scalar.type() != rtti::field_value::kind::boolean) {
            return false;
        }
        v = scalar.as_bool();
        return true;
    }
    bool as_integer(int64_t& v) const noexcept override {
        if (scalar.type() != rtti::field_value::kind::integer) {
            return false;
        }
        v = scalar.as_integer();
        return true;
    }
    bool as_real(double& v) const noexcept override {
        if (scalar.type() != rtti::field_value::kind::real) {
            return false;
        }
        v = scalar.as_real();
        return true;
    }
    bool as_string(rtti::string_view& v) const noexcept override {
        if (scalar.type() != rtti::field_value::kind::string) {
            return false;
        }
        v = scalar.as_string();
        return true;
    }
    std::size_t size() const noexcept override {
        return is_array ? arr.size() : (is_object ? obj.size() : 0);
    }
    rtti::value_io* at(std::size_t i) override {
        return is_array && i < arr.size() ? arr[i] : nullptr;
    }
    rtti::value_io* at(std::size_t i) const override {
        return is_array && i < arr.size() ? arr[i] : nullptr;
    }
    bool get_key(rtti::string_view k, rtti::value_io*& out) override {
        return const_get_key_impl(k, out);
    }
    bool get_key(rtti::string_view k, rtti::value_io*& out) const override {
        return const_get_key_impl(k, out);
    }
    bool get_key_at(std::size_t index, rtti::string_view& key,
                    rtti::value_io*& out) const override {
        if (!is_object || index >= obj.size()) {
            return false;
        }
        auto it = obj.begin();
        std::advance(it, static_cast<std::ptrdiff_t>(index));
        key = rtti::string_view(it->first);
        out = it->second;
        return true;
    }
    rtti::value_io* make_array() override {
        clear_owned();
        is_array = true;
        is_object = false;
        return this;
    }
    void push_back(rtti::value_io* v) override {
        arr.push_back(static_cast<box*>(v));
    }
    rtti::value_io* make_object() override {
        clear_owned();
        is_object = true;
        is_array = false;
        return this;
    }
    void set_key(rtti::string_view k, rtti::value_io* v) override {
        obj[std::string(k)] = static_cast<box*>(v);
    }
    rtti::value_io* make_scalar(rtti::field_value v) override {
        clear_owned();
        if (v.type() == rtti::field_value::kind::string) {
            owned.assign(v.as_string().data(), v.as_string().size());
            scalar = rtti::field_value::of(rtti::string_view(owned));
        } else {
            scalar = v;
        }
        is_object = false;
        is_array = false;
        return this;
    }
    rtti::value_io* make_node() override { return new box; }

private:
    bool const_get_key_impl(rtti::string_view k, rtti::value_io*& out) const {
        if (!is_object) {
            return false;
        }
        auto it = obj.find(std::string(k));
        if (it == obj.end()) {
            return false;
        }
        out = it->second;
        return true;
    }
};

inline box* make_int(int64_t v) {
    auto* b = new box;
    b->make_scalar(rtti::field_value::of(v));
    return b;
}
inline box* make_str(rtti::string_view s) {
    auto* b = new box;
    b->make_scalar(rtti::field_value::of(s));
    return b;
}
inline box* make_arr() {
    auto* b = new box;
    b->make_array();
    return b;
}
inline box* make_obj() {
    auto* b = new box;
    b->make_object();
    return b;
}

} // namespace rtti_test_lib