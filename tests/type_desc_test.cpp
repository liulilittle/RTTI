// Type-description system tests. Extension of the RTTI reflect-core plan:
// rtti::type_of<T>() returns a runtime type_desc covering scalars, strings,
// the four container families, the container adapters, registered structs
// and enums, with recursive element/key types and the struct field table.

#include "test_harness.hpp"
#include <rtti/reflect.hpp>
#include <rtti/convert.hpp>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

struct StructA {
    int a = 0;
    std::string b;
    std::vector<int> c;
    RTTI_DEFINE_TYPE_INTRUSIVE(StructA, a, b, c)
};

struct StructB {
    StructA inner;
    std::map<std::string, StructA> m;
    RTTI_DEFINE_TYPE_INTRUSIVE(StructB, inner, m)
};

struct SelfRef {
    int v = 0;
    std::vector<SelfRef> children;
    RTTI_DEFINE_TYPE_INTRUSIVE(SelfRef, v, children)
};

enum class Color { red, green, blue };
RTTI_DEFINE_ENUM(Color, red, green, blue);

namespace rtti_test_lib {

RTTI_TEST_CASE(type_of_scalars_and_string) {
    const rtti::type_desc* i = rtti::type_of<int>();
    RTTI_CHECK(i->kind == rtti::type_kind::integer);
    RTTI_CHECK_EQ(i->type_name, typeid(int).name());

    RTTI_CHECK(rtti::type_of<double>()->kind == rtti::type_kind::real);
    RTTI_CHECK(rtti::type_of<bool>()->kind == rtti::type_kind::boolean);
    RTTI_CHECK(rtti::type_of<std::string>()->kind == rtti::type_kind::string);
}

RTTI_TEST_CASE(type_of_containers_family_and_element) {
    const rtti::type_desc* v = rtti::type_of<std::vector<int>>();
    RTTI_CHECK(v->kind == rtti::type_kind::container);
    RTTI_CHECK(v->as_container->family == rtti::container_family::vector);
    RTTI_CHECK(v->as_container->element()->kind == rtti::type_kind::integer);

    const rtti::type_desc* m = rtti::type_of<std::map<std::string, int>>();
    RTTI_CHECK(m->as_container->family == rtti::container_family::map);
    RTTI_CHECK(m->as_container->key()->kind == rtti::type_kind::string);
    RTTI_CHECK(m->as_container->element()->kind == rtti::type_kind::integer);

    const rtti::type_desc* s = rtti::type_of<std::set<int>>();
    RTTI_CHECK(s->as_container->family == rtti::container_family::set);
    RTTI_CHECK(s->as_container->element()->kind == rtti::type_kind::integer);

    const rtti::type_desc* u = rtti::type_of<std::unordered_map<std::string, int>>();
    RTTI_CHECK(u->as_container->family == rtti::container_family::unordered_map);

    const rtti::type_desc* l = rtti::type_of<std::vector<int>>();
    (void)l;
}

RTTI_TEST_CASE(type_of_adapters) {
    const rtti::type_desc* q = rtti::type_of<std::queue<int>>();
    RTTI_CHECK(q->kind == rtti::type_kind::container);
    RTTI_CHECK(q->as_container->family == rtti::container_family::queue);
    RTTI_CHECK(q->as_container->element()->kind == rtti::type_kind::integer);

    const rtti::type_desc* st = rtti::type_of<std::stack<std::string>>();
    RTTI_CHECK(st->as_container->family == rtti::container_family::stack);
    RTTI_CHECK(st->as_container->element()->kind == rtti::type_kind::string);

    const rtti::type_desc* pq = rtti::type_of<std::priority_queue<int>>();
    RTTI_CHECK(pq->as_container->family == rtti::container_family::priority_queue);
}

RTTI_TEST_CASE(type_of_reflected_struct_fields) {
    const rtti::type_desc* a = rtti::type_of<StructA>();
    RTTI_CHECK(a->kind == rtti::type_kind::reflected);
    RTTI_CHECK_EQ(a->as_reflected->field_count, size_t(3));
    RTTI_CHECK_EQ(std::string(a->as_reflected->fields[0].name), std::string("a"));
    RTTI_CHECK(a->as_reflected->fields[0].type()->kind == rtti::type_kind::integer);
    RTTI_CHECK_EQ(std::string(a->as_reflected->fields[1].name), std::string("b"));
    RTTI_CHECK(a->as_reflected->fields[1].type()->kind == rtti::type_kind::string);
    RTTI_CHECK(a->as_reflected->fields[2].type()->kind == rtti::type_kind::container);
    RTTI_CHECK(a->as_reflected->fields[2].type()->as_container->family ==
               rtti::container_family::vector);
}

RTTI_TEST_CASE(type_of_nested_reflection_recursive) {
    const rtti::type_desc* b = rtti::type_of<StructB>();
    RTTI_CHECK(b->kind == rtti::type_kind::reflected);
    const rtti::type_desc* inner = b->as_reflected->fields[0].type();
    RTTI_CHECK(inner->kind == rtti::type_kind::reflected);
    RTTI_CHECK_EQ(inner->as_reflected->field_count, size_t(3));
    const rtti::type_desc* m = b->as_reflected->fields[1].type();
    RTTI_CHECK(m->as_container->family == rtti::container_family::map);
    RTTI_CHECK(m->as_container->element()->kind == rtti::type_kind::reflected);
}

RTTI_TEST_CASE(type_of_self_reference_no_loop) {
    const rtti::type_desc* s = rtti::type_of<SelfRef>();
    RTTI_CHECK(s->kind == rtti::type_kind::reflected);
    const rtti::type_desc* children = s->as_reflected->fields[1].type();
    RTTI_CHECK(children->kind == rtti::type_kind::container);
    RTTI_CHECK(children->as_container->element()->kind == rtti::type_kind::reflected);
}

RTTI_TEST_CASE(type_of_enum) {
    const rtti::type_desc* e = rtti::type_of<Color>();
    RTTI_CHECK(e->kind == rtti::type_kind::enum_type);
    RTTI_CHECK_EQ(e->as_enum->count, size_t(3));
    RTTI_CHECK_EQ(std::string(e->as_enum->names[0]), std::string("red"));
    RTTI_CHECK_EQ(std::string(e->as_enum->names[2]), std::string("blue"));
}

RTTI_TEST_CASE(type_of_from_object) {
    StructA a;
    const rtti::type_desc* d = rtti::type_of(a);
    RTTI_CHECK(d->kind == rtti::type_kind::reflected);
    StructB b2;
    RTTI_CHECK(rtti::type_of(b2)->kind == rtti::type_kind::reflected);
}

RTTI_TEST_CASE(field_type_query_runtime) {
    const rtti::field_access_base* f = rtti::find_field<StructA>("c");
    RTTI_CHECK(f != nullptr);
    RTTI_CHECK(f->type == &typeid(std::vector<int>));
}

} // namespace rtti_test_lib