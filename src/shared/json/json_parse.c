#include "openbc/json_parse.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* --- Tokenizer --- */

typedef struct {
    const char *pos;
} parser_t;

static void skip_ws(parser_t *p)
{
    while (*p->pos && isspace((unsigned char)*p->pos)) p->pos++;
}

static bool match_str(parser_t *p, const char *s)
{
    size_t len = strlen(s);
    if (strncmp(p->pos, s, len) == 0) { p->pos += len; return true; }
    return false;
}

/* Parse a JSON string (expects opening " already consumed or about to be).
 * Returns malloc'd string or NULL on error. */
static char *parse_string_raw(parser_t *p)
{
    skip_ws(p);
    if (*p->pos != '"') return NULL;
    p->pos++; /* skip opening " */

    /* Find length first (simple: no unicode escapes) */
    const char *start = p->pos;
    size_t len = 0;
    const char *scan = start;
    while (*scan && *scan != '"') {
        if (*scan == '\\') { scan++; } /* skip escaped char */
        scan++;
        len++;
    }
    if (*scan != '"') return NULL;

    char *result = malloc(len + 1);
    if (!result) return NULL;

    size_t i = 0;
    while (*p->pos != '"') {
        if (*p->pos == '\\') {
            p->pos++;
            switch (*p->pos) {
                case '"':  result[i++] = '"';  break;
                case '\\': result[i++] = '\\'; break;
                case '/':  result[i++] = '/';  break;
                case 'n':  result[i++] = '\n'; break;
                case 't':  result[i++] = '\t'; break;
                case 'r':  result[i++] = '\r'; break;
                default:   result[i++] = *p->pos; break;
            }
        } else {
            result[i++] = *p->pos;
        }
        p->pos++;
    }
    result[i] = '\0';
    p->pos++; /* skip closing " */
    return result;
}

/* Forward declaration */
static json_value_t *parse_value(parser_t *p);

static json_value_t *alloc_value(json_type_t type)
{
    json_value_t *v = calloc(1, sizeof(json_value_t));
    if (v) v->type = type;
    return v;
}

static json_value_t *parse_string(parser_t *p)
{
    char *s = parse_string_raw(p);
    if (!s) return NULL;
    json_value_t *v = alloc_value(JSON_STRING);
    if (!v) { free(s); return NULL; }
    v->string = s;
    return v;
}

static json_value_t *parse_number(parser_t *p)
{
    char *end;
    double d = strtod(p->pos, &end);
    if (end == p->pos) return NULL;
    p->pos = end;
    json_value_t *v = alloc_value(JSON_NUMBER);
    if (!v) return NULL;
    v->number = d;
    return v;
}

static json_value_t *parse_array(parser_t *p)
{
    /* Opening [ already consumed */
    json_value_t *v = alloc_value(JSON_ARRAY);
    if (!v) return NULL;

    size_t cap = 8;
    v->array.items = malloc(cap * sizeof(json_value_t *));
    v->array.count = 0;
    if (!v->array.items) { free(v); return NULL; }

    skip_ws(p);
    if (*p->pos == ']') { p->pos++; return v; }

    for (;;) {
        json_value_t *item = parse_value(p);
        if (!item) { json_free(v); return NULL; }

        if (v->array.count >= cap) {
            cap *= 2;
            json_value_t **tmp = realloc(v->array.items, cap * sizeof(json_value_t *));
            if (!tmp) { json_free(item); json_free(v); return NULL; }
            v->array.items = tmp;
        }
        v->array.items[v->array.count++] = item;

        skip_ws(p);
        if (*p->pos == ',') { p->pos++; continue; }
        if (*p->pos == ']') { p->pos++; return v; }
        json_free(v);
        return NULL;
    }
}

static json_value_t *parse_object(parser_t *p)
{
    /* Opening { already consumed */
    json_value_t *v = alloc_value(JSON_OBJECT);
    if (!v) return NULL;

    size_t cap = 8;
    v->object.members = malloc(cap * sizeof(json_member_t));
    v->object.count = 0;
    if (!v->object.members) { free(v); return NULL; }

    skip_ws(p);
    if (*p->pos == '}') { p->pos++; return v; }

    for (;;) {
        char *key = parse_string_raw(p);
        if (!key) { json_free(v); return NULL; }

        skip_ws(p);
        if (*p->pos != ':') { free(key); json_free(v); return NULL; }
        p->pos++;

        json_value_t *val = parse_value(p);
        if (!val) { free(key); json_free(v); return NULL; }

        if (v->object.count >= cap) {
            cap *= 2;
            json_member_t *tmp = realloc(v->object.members, cap * sizeof(json_member_t));
            if (!tmp) { free(key); json_free(val); json_free(v); return NULL; }
            v->object.members = tmp;
        }
        v->object.members[v->object.count].key = key;
        v->object.members[v->object.count].value = val;
        v->object.count++;

        skip_ws(p);
        if (*p->pos == ',') { p->pos++; continue; }
        if (*p->pos == '}') { p->pos++; return v; }
        json_free(v);
        return NULL;
    }
}

static json_value_t *parse_value(parser_t *p)
{
    skip_ws(p);

    switch (*p->pos) {
    case '"':
        return parse_string(p);
    case '{':
        p->pos++;
        return parse_object(p);
    case '[':
        p->pos++;
        return parse_array(p);
    case 't':
        if (match_str(p, "true")) {
            json_value_t *v = alloc_value(JSON_BOOL);
            if (v) v->boolean = true;
            return v;
        }
        return NULL;
    case 'f':
        if (match_str(p, "false")) {
            json_value_t *v = alloc_value(JSON_BOOL);
            if (v) v->boolean = false;
            return v;
        }
        return NULL;
    case 'n':
        if (match_str(p, "null")) return alloc_value(JSON_NULL);
        return NULL;
    default:
        if (*p->pos == '-' || isdigit((unsigned char)*p->pos))
            return parse_number(p);
        return NULL;
    }
}

/* --- Public API --- */

json_value_t *json_parse(const char *text)
{
    parser_t p = { .pos = text };
    json_value_t *root = parse_value(&p);
    return root;
}

void json_free(json_value_t *val)
{
    if (!val) return;
    switch (val->type) {
    case JSON_STRING:
        free(val->string);
        break;
    case JSON_ARRAY:
        for (size_t i = 0; i < val->array.count; i++)
            json_free(val->array.items[i]);
        free(val->array.items);
        break;
    case JSON_OBJECT:
        for (size_t i = 0; i < val->object.count; i++) {
            free(val->object.members[i].key);
            json_free(val->object.members[i].value);
        }
        free(val->object.members);
        break;
    default:
        break;
    }
    free(val);
}

json_value_t *json_get(const json_value_t *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < obj->object.count; i++) {
        if (strcmp(obj->object.members[i].key, key) == 0)
            return obj->object.members[i].value;
    }
    return NULL;
}

size_t json_array_len(const json_value_t *arr)
{
    if (!arr || arr->type != JSON_ARRAY) return 0;
    return arr->array.count;
}

json_value_t *json_array_get(const json_value_t *arr, size_t index)
{
    if (!arr || arr->type != JSON_ARRAY || index >= arr->array.count) return NULL;
    return arr->array.items[index];
}

const char *json_string(const json_value_t *val)
{
    if (!val || val->type != JSON_STRING) return NULL;
    return val->string;
}

double json_number(const json_value_t *val)
{
    if (!val || val->type != JSON_NUMBER) return 0.0;
    return val->number;
}

bool json_bool(const json_value_t *val)
{
    if (!val || val->type != JSON_BOOL) return false;
    return val->boolean;
}

int json_int(const json_value_t *val)
{
    if (!val || val->type != JSON_NUMBER) return 0;
    return (int)val->number;
}
