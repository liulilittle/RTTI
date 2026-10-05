// Error-infrastructure tests. Task 2 of the RTTI reflect-core plan.
// Covers error_code strings, result<T> semantics, and lazy line/column.

#include "test_harness.hpp"
#include <rtti/error.hpp>

RTTI_TEST_CASE(error_code_strings) {
    RTTI_CHECK_EQ(std::string(rtti::to_string(rtti::error_code::ok)),
                  std::string("ok"));
    RTTI_CHECK_EQ(std::string(rtti::to_string(rtti::error_code::no_such_field)),
                  std::string("no such field"));
    RTTI_CHECK_EQ(std::string(rtti::to_string(rtti::error_code::kind_mismatch)),
                  std::string("kind mismatch"));
}

RTTI_TEST_CASE(result_bool_and_value) {
    rtti::result<int> r;
    r.value = 42;
    RTTI_CHECK(r.ok());
    RTTI_CHECK_EQ(*r, 42);

    rtti::result<int> bad;
    bad.err.code = rtti::error_code::unexpected_token;
    RTTI_CHECK(!bad.ok());
    RTTI_CHECK(!bad);
}

RTTI_TEST_CASE(result_void_specialization) {
    rtti::result<void> ok;
    RTTI_CHECK(ok.ok());
    rtti::result<void> bad;
    bad.err.code = rtti::error_code::bad_entity;
    RTTI_CHECK(!bad.ok());
}

RTTI_TEST_CASE(lazy_line_column) {
    const char* src = "a\nbb\nccc";
    rtti::error_info e;
    e.src = src;
    e.src_len = 8;
    // Index map: 0='a' 1='\n' 2='b' 3='b' 4='\n' 5='c' 6='c' 7='c'
    e.offset = 7; // last 'c' is on line 3, column 3
    RTTI_CHECK_EQ(e.line(), size_t(3));
    RTTI_CHECK_EQ(e.column(), size_t(3));
    e.offset = 5; // first 'c' is on line 3, column 1
    RTTI_CHECK_EQ(e.line(), size_t(3));
    RTTI_CHECK_EQ(e.column(), size_t(1));
}

RTTI_TEST_CASE(error_info_message_matches_code) {
    rtti::error_info e;
    e.code = rtti::error_code::unterminated_string;
    RTTI_CHECK_EQ(std::string(e.message()), std::string("unterminated string"));
}
