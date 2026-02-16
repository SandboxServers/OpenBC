#include "test_util.h"
#include "openbc/json_parse.h"
#include <string.h>

/* === JSON parser tests === */

TEST(json_parse_null)
{
    json_value_t *v = json_parse("null");
    ASSERT(v != NULL);
    ASSERT_EQ_INT(v->type, JSON_NULL);
    json_free(v);
}

TEST(json_parse_true)
{
    json_value_t *v = json_parse("true");
    ASSERT(v != NULL);
    ASSERT_EQ_INT(v->type, JSON_BOOL);
    ASSERT(json_bool(v) == true);
    json_free(v);
}

TEST(json_parse_false)
{
    json_value_t *v = json_parse("false");
    ASSERT(v != NULL);
    ASSERT_EQ_INT(v->type, JSON_BOOL);
    ASSERT(json_bool(v) == false);
    json_free(v);
}

TEST(json_parse_number)
{
    json_value_t *v = json_parse("42");
    ASSERT(v != NULL);
    ASSERT_EQ_INT(v->type, JSON_NUMBER);
    ASSERT_EQ_INT(json_int(v), 42);
    json_free(v);
}

TEST(json_parse_negative_number)
{
    json_value_t *v = json_parse("-7");
    ASSERT(v != NULL);
    ASSERT_EQ_INT(json_int(v), -7);
    json_free(v);
}

TEST(json_parse_string)
{
    json_value_t *v = json_parse("\"hello\"");
    ASSERT(v != NULL);
    ASSERT_EQ_INT(v->type, JSON_STRING);
    ASSERT(strcmp(json_string(v), "hello") == 0);
    json_free(v);
}

TEST(json_parse_string_escape)
{
    json_value_t *v = json_parse("\"line\\none\"");
    ASSERT(v != NULL);
    ASSERT(strcmp(json_string(v), "line\none") == 0);
    json_free(v);
}

TEST(json_parse_empty_object)
{
    json_value_t *v = json_parse("{}");
    ASSERT(v != NULL);
    ASSERT_EQ_INT(v->type, JSON_OBJECT);
    ASSERT(json_get(v, "nope") == NULL);
    json_free(v);
}

TEST(json_parse_empty_array)
{
    json_value_t *v = json_parse("[]");
    ASSERT(v != NULL);
    ASSERT_EQ_INT(v->type, JSON_ARRAY);
    ASSERT_EQ_INT((int)json_array_len(v), 0);
    json_free(v);
}

TEST(json_parse_simple_object)
{
    json_value_t *v = json_parse("{\"name\": \"scripts\", \"index\": 0}");
    ASSERT(v != NULL);
    ASSERT(strcmp(json_string(json_get(v, "name")), "scripts") == 0);
    ASSERT_EQ_INT(json_int(json_get(v, "index")), 0);
    json_free(v);
}

TEST(json_parse_simple_array)
{
    json_value_t *v = json_parse("[1, 2, 3]");
    ASSERT(v != NULL);
    ASSERT_EQ_INT((int)json_array_len(v), 3);
    ASSERT_EQ_INT(json_int(json_array_get(v, 0)), 1);
    ASSERT_EQ_INT(json_int(json_array_get(v, 1)), 2);
    ASSERT_EQ_INT(json_int(json_array_get(v, 2)), 3);
    json_free(v);
}

TEST(json_parse_nested)
{
    const char *text =
        "{"
        "  \"meta\": {\"name\": \"test\"},"
        "  \"files\": ["
        "    {\"filename\": \"App.pyc\", \"hash\": \"0x373EB677\"}"
        "  ]"
        "}";

    json_value_t *v = json_parse(text);
    ASSERT(v != NULL);

    /* Nested object */
    json_value_t *meta = json_get(v, "meta");
    ASSERT(meta != NULL);
    ASSERT(strcmp(json_string(json_get(meta, "name")), "test") == 0);

    /* Array of objects */
    json_value_t *files = json_get(v, "files");
    ASSERT_EQ_INT((int)json_array_len(files), 1);
    json_value_t *f0 = json_array_get(files, 0);
    ASSERT(strcmp(json_string(json_get(f0, "filename")), "App.pyc") == 0);
    ASSERT(strcmp(json_string(json_get(f0, "hash")), "0x373EB677") == 0);

    json_free(v);
}

TEST(json_parse_bool_values)
{
    json_value_t *v = json_parse("{\"recursive\": true, \"enabled\": false}");
    ASSERT(v != NULL);
    ASSERT(json_bool(json_get(v, "recursive")) == true);
    ASSERT(json_bool(json_get(v, "enabled")) == false);
    json_free(v);
}

TEST(json_accessor_type_mismatch)
{
    json_value_t *v = json_parse("42");
    ASSERT(v != NULL);
    /* Asking for string from a number should return NULL */
    ASSERT(json_string(v) == NULL);
    /* Asking for bool from a number should return false */
    ASSERT(json_bool(v) == false);
    /* Array ops on non-array */
    ASSERT_EQ_INT((int)json_array_len(v), 0);
    ASSERT(json_array_get(v, 0) == NULL);
    /* Object get on non-object */
    ASSERT(json_get(v, "key") == NULL);
    json_free(v);
}

TEST(json_parse_invalid)
{
    ASSERT(json_parse("") == NULL);
    ASSERT(json_parse("{broken") == NULL);
    ASSERT(json_parse("[1, 2,]") == NULL);  /* trailing comma */
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    RUN(json_parse_null);
    RUN(json_parse_true);
    RUN(json_parse_false);
    RUN(json_parse_number);
    RUN(json_parse_negative_number);
    RUN(json_parse_string);
    RUN(json_parse_string_escape);
    RUN(json_parse_empty_object);
    RUN(json_parse_empty_array);
    RUN(json_parse_simple_object);
    RUN(json_parse_simple_array);
    RUN(json_parse_nested);
    RUN(json_parse_bool_values);
    RUN(json_accessor_type_mismatch);
    RUN(json_parse_invalid);
TEST_MAIN_END()
