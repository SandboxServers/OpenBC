#ifndef OPENBC_JSON_PARSE_H
#define OPENBC_JSON_PARSE_H

#include "openbc/types.h"
#include <stddef.h>

/* Minimal DOM-style JSON parser -- just enough for manifest verification.
 * No unicode escape handling, no number exponents, no deeply nested edge cases.
 * Designed for machine-generated JSON from our own manifest tool. */

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} json_type_t;

typedef struct json_value json_value_t;

/* Object member (key-value pair) */
typedef struct {
    char         *key;
    json_value_t *value;
} json_member_t;

struct json_value {
    json_type_t type;
    union {
        bool          boolean;
        double        number;
        char         *string;
        struct {
            json_value_t **items;
            size_t         count;
        } array;
        struct {
            json_member_t *members;
            size_t         count;
        } object;
    };
};

/* Parse a JSON string into a value tree.  Returns NULL on parse error. */
json_value_t *json_parse(const char *text);

/* Free a value tree. */
void json_free(json_value_t *val);

/* Accessors -- return NULL / 0 / false on type mismatch or missing key. */
json_value_t  *json_get(const json_value_t *obj, const char *key);
size_t          json_array_len(const json_value_t *arr);
json_value_t  *json_array_get(const json_value_t *arr, size_t index);
const char    *json_string(const json_value_t *val);
double          json_number(const json_value_t *val);
bool            json_bool(const json_value_t *val);
int             json_int(const json_value_t *val);

#endif /* OPENBC_JSON_PARSE_H */
