/* A small JSON implementation: values, parser and serializer. */
#include "internal.h"

#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 512

struct ollama_json {
    ollama_json_type_t type;
    union {
        bool bool_value;
        struct {
            double value;
            int64_t ivalue;
            bool is_int;
        } number;
        struct {
            char *data;
            size_t len;
        } string;
        struct {
            ollama_json_t **items;
            char **keys; /* objects only */
            size_t count;
            size_t cap;
        } container;
    } u;
};

/* ---- construction ---- */

static ollama_json_t *json_new(ollama_json_type_t type) {
    ollama_json_t *v = calloc(1, sizeof(*v));
    if (v) v->type = type;
    return v;
}

ollama_json_t *ollama_json_null(void) { return json_new(OLLAMA_JSON_NULL); }

ollama_json_t *ollama_json_bool(bool value) {
    ollama_json_t *v = json_new(OLLAMA_JSON_BOOL);
    if (v) v->u.bool_value = value;
    return v;
}

ollama_json_t *ollama_json_number(double value) {
    ollama_json_t *v = json_new(OLLAMA_JSON_NUMBER);
    if (v) {
        v->u.number.value = value;
        v->u.number.ivalue = (value >= -9.2e18 && value <= 9.2e18) ? (int64_t)value : 0;
    }
    return v;
}

ollama_json_t *ollama_json_int(int64_t value) {
    ollama_json_t *v = json_new(OLLAMA_JSON_NUMBER);
    if (v) {
        v->u.number.value = (double)value;
        v->u.number.ivalue = value;
        v->u.number.is_int = true;
    }
    return v;
}

static ollama_json_t *string_take(char *data, size_t len) {
    ollama_json_t *v = json_new(OLLAMA_JSON_STRING);
    if (!v) {
        free(data);
        return NULL;
    }
    v->u.string.data = data;
    v->u.string.len = len;
    return v;
}

ollama_json_t *ollama_json_string(const char *value) {
    if (!value) return NULL;
    size_t len = strlen(value);
    char *data = ol_strndup(value, len);
    return data ? string_take(data, len) : NULL;
}

ollama_json_t *ollama_json_array(void) { return json_new(OLLAMA_JSON_ARRAY); }

ollama_json_t *ollama_json_object(void) { return json_new(OLLAMA_JSON_OBJECT); }

void ollama_json_free(ollama_json_t *v) {
    if (!v) return;
    if (v->type == OLLAMA_JSON_STRING) {
        free(v->u.string.data);
    } else if (v->type == OLLAMA_JSON_ARRAY || v->type == OLLAMA_JSON_OBJECT) {
        for (size_t i = 0; i < v->u.container.count; i++) {
            ollama_json_free(v->u.container.items[i]);
            if (v->u.container.keys) free(v->u.container.keys[i]);
        }
        free(v->u.container.items);
        free(v->u.container.keys);
    }
    free(v);
}

static bool container_grow(ollama_json_t *c) {
    if (c->u.container.count < c->u.container.cap) return true;
    size_t cap = c->u.container.cap ? c->u.container.cap * 2 : 4;
    if (cap > SIZE_MAX / sizeof(void *)) return false;
    ollama_json_t **items = realloc(c->u.container.items, cap * sizeof(*items));
    if (!items) return false;
    c->u.container.items = items;
    if (c->type == OLLAMA_JSON_OBJECT) {
        char **keys = realloc(c->u.container.keys, cap * sizeof(*keys));
        if (!keys) return false;
        c->u.container.keys = keys;
    }
    c->u.container.cap = cap;
    return true;
}

ollama_json_t *ollama_json_append(ollama_json_t *array, ollama_json_t *value) {
    if (!value) return NULL;
    if (!array || array->type != OLLAMA_JSON_ARRAY || !container_grow(array)) {
        ollama_json_free(value);
        return NULL;
    }
    array->u.container.items[array->u.container.count++] = value;
    return value;
}

static ptrdiff_t object_find(const ollama_json_t *o, const char *key) {
    /* the last occurrence wins, like most JSON decoders */
    for (size_t i = o->u.container.count; i > 0; i--)
        if (strcmp(o->u.container.keys[i - 1], key) == 0) return (ptrdiff_t)(i - 1);
    return -1;
}

/* Appends a member without looking for duplicates; takes ownership of key and value. */
static bool object_push(ollama_json_t *o, char *key, ollama_json_t *value) {
    if (!container_grow(o)) {
        free(key);
        ollama_json_free(value);
        return false;
    }
    o->u.container.keys[o->u.container.count] = key;
    o->u.container.items[o->u.container.count++] = value;
    return true;
}

ollama_json_t *ollama_json_set(ollama_json_t *object, const char *key, ollama_json_t *value) {
    if (!value) return NULL;
    if (!object || object->type != OLLAMA_JSON_OBJECT || !key) {
        ollama_json_free(value);
        return NULL;
    }
    ptrdiff_t i = object_find(object, key);
    if (i >= 0) {
        ollama_json_free(object->u.container.items[i]);
        object->u.container.items[i] = value;
        return value;
    }
    char *k = ol_strdup(key);
    if (!k) {
        ollama_json_free(value);
        return NULL;
    }
    return object_push(object, k, value) ? value : NULL;
}

ollama_json_t *ollama_json_set_string(ollama_json_t *object, const char *key, const char *value) {
    return ollama_json_set(object, key, ollama_json_string(value));
}

ollama_json_t *ollama_json_set_number(ollama_json_t *object, const char *key, double value) {
    return ollama_json_set(object, key, ollama_json_number(value));
}

ollama_json_t *ollama_json_set_int(ollama_json_t *object, const char *key, int64_t value) {
    return ollama_json_set(object, key, ollama_json_int(value));
}

ollama_json_t *ollama_json_set_bool(ollama_json_t *object, const char *key, bool value) {
    return ollama_json_set(object, key, ollama_json_bool(value));
}

ollama_json_t *ollama_json_copy(const ollama_json_t *v) {
    if (!v) return NULL;
    switch (v->type) {
    case OLLAMA_JSON_NULL: return ollama_json_null();
    case OLLAMA_JSON_BOOL: return ollama_json_bool(v->u.bool_value);
    case OLLAMA_JSON_NUMBER: {
        ollama_json_t *n = json_new(OLLAMA_JSON_NUMBER);
        if (n) n->u.number = v->u.number;
        return n;
    }
    case OLLAMA_JSON_STRING: {
        char *data = ol_strndup(v->u.string.data, v->u.string.len);
        return data ? string_take(data, v->u.string.len) : NULL;
    }
    case OLLAMA_JSON_ARRAY:
    case OLLAMA_JSON_OBJECT: {
        ollama_json_t *c = json_new(v->type);
        if (!c) return NULL;
        for (size_t i = 0; i < v->u.container.count; i++) {
            ollama_json_t *item = ollama_json_copy(v->u.container.items[i]);
            bool ok;
            if (v->type == OLLAMA_JSON_ARRAY) {
                ok = ollama_json_append(c, item) != NULL;
            } else {
                char *key = ol_strdup(v->u.container.keys[i]);
                ok = item && key && object_push(c, key, item);
                if (!ok && (!item || !key)) {
                    free(key);
                    ollama_json_free(item);
                }
            }
            if (!ok) {
                ollama_json_free(c);
                return NULL;
            }
        }
        return c;
    }
    }
    return NULL;
}

/* ---- access ---- */

ollama_json_type_t ollama_json_type(const ollama_json_t *v) { return v ? v->type : OLLAMA_JSON_NULL; }

const ollama_json_t *ollama_json_get(const ollama_json_t *object, const char *key) {
    if (!object || object->type != OLLAMA_JSON_OBJECT || !key) return NULL;
    ptrdiff_t i = object_find(object, key);
    return i >= 0 ? object->u.container.items[i] : NULL;
}

size_t ollama_json_size(const ollama_json_t *v) {
    if (!v || (v->type != OLLAMA_JSON_ARRAY && v->type != OLLAMA_JSON_OBJECT)) return 0;
    return v->u.container.count;
}

const ollama_json_t *ollama_json_at(const ollama_json_t *v, size_t index) {
    if (index >= ollama_json_size(v)) return NULL;
    return v->u.container.items[index];
}

const char *ollama_json_key_at(const ollama_json_t *object, size_t index) {
    if (!object || object->type != OLLAMA_JSON_OBJECT || index >= object->u.container.count) return NULL;
    return object->u.container.keys[index];
}

const char *ollama_json_as_string(const ollama_json_t *v) {
    return v && v->type == OLLAMA_JSON_STRING ? v->u.string.data : NULL;
}

double ollama_json_as_number(const ollama_json_t *v) {
    return v && v->type == OLLAMA_JSON_NUMBER ? v->u.number.value : 0.0;
}

int64_t ollama_json_as_int(const ollama_json_t *v) {
    return v && v->type == OLLAMA_JSON_NUMBER ? v->u.number.ivalue : 0;
}

bool ollama_json_as_bool(const ollama_json_t *v) { return v && v->type == OLLAMA_JSON_BOOL && v->u.bool_value; }

/* ---- locale independent number conversion ---- */

static const char *decimal_point(void) {
    const char *dp = localeconv()->decimal_point;
    return dp && *dp ? dp : ".";
}

/* Converts the JSON number text [s, s+len) to a double, whatever LC_NUMERIC is. */
static double number_from_text(const char *s, size_t len) {
    const char *dp = decimal_point();
    size_t dplen = strlen(dp);
    char stack_buf[128];
    size_t cap = len * dplen + 1;
    char *tmp = cap <= sizeof(stack_buf) ? stack_buf : malloc(cap);
    if (!tmp) return 0.0;
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '.') {
            memcpy(tmp + n, dp, dplen);
            n += dplen;
        } else {
            tmp[n++] = s[i];
        }
    }
    tmp[n] = '\0';
    double d = strtod(tmp, NULL);
    if (tmp != stack_buf) free(tmp);
    return d;
}

static void format_double(char *out, size_t size, double d) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%.15g", d);
    if (number_from_text(tmp, strlen(tmp)) != d) snprintf(tmp, sizeof(tmp), "%.17g", d);
    /* replace the locale's decimal point with '.' */
    const char *dp = decimal_point();
    size_t dplen = strlen(dp), n = 0;
    for (const char *p = tmp; *p && n + 1 < size;) {
        if (strncmp(p, dp, dplen) == 0) {
            out[n++] = '.';
            p += dplen;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = '\0';
}

/* ---- serializer ---- */

static bool write_string(buf_t *b, const char *s, size_t len) {
    static const char hex[] = "0123456789abcdef";
    buf_putc(b, '"');
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c != '"' && c != '\\') continue;
        buf_append(b, s + start, i - start);
        start = i + 1;
        switch (c) {
        case '"': buf_puts(b, "\\\""); break;
        case '\\': buf_puts(b, "\\\\"); break;
        case '\b': buf_puts(b, "\\b"); break;
        case '\f': buf_puts(b, "\\f"); break;
        case '\n': buf_puts(b, "\\n"); break;
        case '\r': buf_puts(b, "\\r"); break;
        case '\t': buf_puts(b, "\\t"); break;
        default: {
            char esc[7] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15], '\0'};
            buf_puts(b, esc);
        }
        }
    }
    buf_append(b, s + start, len - start);
    return buf_putc(b, '"');
}

bool json_write(buf_t *b, const ollama_json_t *v) {
    if (!v) return buf_puts(b, "null");
    switch (v->type) {
    case OLLAMA_JSON_NULL: return buf_puts(b, "null");
    case OLLAMA_JSON_BOOL: return buf_puts(b, v->u.bool_value ? "true" : "false");
    case OLLAMA_JSON_NUMBER: {
        char num[64];
        if (v->u.number.is_int)
            snprintf(num, sizeof(num), "%" PRId64, v->u.number.ivalue);
        else if (!isfinite(v->u.number.value))
            strcpy(num, "null");
        else
            format_double(num, sizeof(num), v->u.number.value);
        return buf_puts(b, num);
    }
    case OLLAMA_JSON_STRING: return write_string(b, v->u.string.data, v->u.string.len);
    case OLLAMA_JSON_ARRAY:
    case OLLAMA_JSON_OBJECT: {
        bool object = v->type == OLLAMA_JSON_OBJECT;
        buf_putc(b, object ? '{' : '[');
        for (size_t i = 0; i < v->u.container.count; i++) {
            if (i) buf_putc(b, ',');
            if (object) {
                write_string(b, v->u.container.keys[i], strlen(v->u.container.keys[i]));
                buf_putc(b, ':');
            }
            json_write(b, v->u.container.items[i]);
        }
        return buf_putc(b, object ? '}' : ']');
    }
    }
    return false;
}

char *ollama_json_stringify(const ollama_json_t *value) {
    buf_t b = {0};
    if (!json_write(&b, value) || !buf_cstr(&b)) {
        buf_free(&b);
        return NULL;
    }
    return b.data;
}

/* ---- parser ---- */

typedef struct parser {
    const char *p;
    const char *end;
    int depth;
} parser_t;

static void skip_ws(parser_t *ps) {
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) ps->p++;
}

static bool literal(parser_t *ps, const char *word) {
    size_t n = strlen(word);
    if ((size_t)(ps->end - ps->p) < n || memcmp(ps->p, word, n) != 0) return false;
    ps->p += n;
    return true;
}

static int hex4(const char *p) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

static void put_utf8(buf_t *b, uint32_t cp) {
    char out[4];
    size_t n;
    if (cp < 0x80) {
        out[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xc0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 0x3f));
        n = 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xe0 | cp >> 12);
        out[1] = (char)(0x80 | (cp >> 6 & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        n = 3;
    } else {
        out[0] = (char)(0xf0 | cp >> 18);
        out[1] = (char)(0x80 | (cp >> 12 & 0x3f));
        out[2] = (char)(0x80 | (cp >> 6 & 0x3f));
        out[3] = (char)(0x80 | (cp & 0x3f));
        n = 4;
    }
    buf_append(b, out, n);
}

/* Parses a string starting at the opening quote into a malloc'ed buffer. */
static char *parse_string(parser_t *ps, size_t *len) {
    buf_t b = {0};
    ps->p++; /* opening quote */
    for (;;) {
        const char *start = ps->p;
        while (ps->p < ps->end && *ps->p != '"' && *ps->p != '\\' && (unsigned char)*ps->p >= 0x20) ps->p++;
        buf_append(&b, start, (size_t)(ps->p - start));
        if (ps->p >= ps->end || (unsigned char)*ps->p < 0x20) goto fail;
        if (*ps->p == '"') {
            ps->p++;
            break;
        }
        /* escape sequence */
        if (++ps->p >= ps->end) goto fail;
        char c = *ps->p++;
        switch (c) {
        case '"': buf_putc(&b, '"'); break;
        case '\\': buf_putc(&b, '\\'); break;
        case '/': buf_putc(&b, '/'); break;
        case 'b': buf_putc(&b, '\b'); break;
        case 'f': buf_putc(&b, '\f'); break;
        case 'n': buf_putc(&b, '\n'); break;
        case 'r': buf_putc(&b, '\r'); break;
        case 't': buf_putc(&b, '\t'); break;
        case 'u': {
            if (ps->end - ps->p < 4) goto fail;
            int cp = hex4(ps->p);
            if (cp < 0) goto fail;
            ps->p += 4;
            uint32_t code = (uint32_t)cp;
            if (code >= 0xd800 && code <= 0xdbff) {
                int low = -1;
                if (ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u') low = hex4(ps->p + 2);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    code = 0x10000 + ((code - 0xd800) << 10) + ((uint32_t)low - 0xdc00);
                    ps->p += 6;
                } else {
                    code = 0xfffd;
                }
            } else if (code >= 0xdc00 && code <= 0xdfff) {
                code = 0xfffd;
            }
            put_utf8(&b, code);
            break;
        }
        default: goto fail;
        }
    }
    if (!buf_cstr(&b)) goto fail;
    *len = b.len;
    return b.data;
fail:
    buf_free(&b);
    return NULL;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static ollama_json_t *parse_number(parser_t *ps) {
    const char *start = ps->p;
    bool is_int = true;
    if (ps->p < ps->end && *ps->p == '-') ps->p++;
    if (ps->p >= ps->end) return NULL;
    if (*ps->p == '0') {
        ps->p++;
    } else if (is_digit(*ps->p)) {
        while (ps->p < ps->end && is_digit(*ps->p)) ps->p++;
    } else {
        return NULL;
    }
    if (ps->p < ps->end && *ps->p == '.') {
        is_int = false;
        ps->p++;
        if (ps->p >= ps->end || !is_digit(*ps->p)) return NULL;
        while (ps->p < ps->end && is_digit(*ps->p)) ps->p++;
    }
    if (ps->p < ps->end && (*ps->p == 'e' || *ps->p == 'E')) {
        is_int = false;
        ps->p++;
        if (ps->p < ps->end && (*ps->p == '+' || *ps->p == '-')) ps->p++;
        if (ps->p >= ps->end || !is_digit(*ps->p)) return NULL;
        while (ps->p < ps->end && is_digit(*ps->p)) ps->p++;
    }
    size_t len = (size_t)(ps->p - start);
    if (is_int) {
        /* exact 64-bit integers, falling back to double on overflow */
        bool negative = *start == '-';
        uint64_t limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
        uint64_t acc = 0;
        for (const char *p = negative ? start + 1 : start; p < ps->p; p++) {
            unsigned digit = (unsigned)(*p - '0');
            if (acc > (limit - digit) / 10) {
                is_int = false;
                break;
            }
            acc = acc * 10 + digit;
        }
        /* "-0" is not an integer: as a double it keeps its sign through a round trip */
        if (is_int && !(negative && acc == 0)) {
            int64_t value = negative ? (acc == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)acc) : (int64_t)acc;
            return ollama_json_int(value);
        }
    }
    return ollama_json_number(number_from_text(start, len));
}

static ollama_json_t *parse_value(parser_t *ps);

static ollama_json_t *parse_array(parser_t *ps) {
    ollama_json_t *arr = ollama_json_array();
    if (!arr) return NULL;
    ps->p++; /* [ */
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ']') {
        ps->p++;
        return arr;
    }
    for (;;) {
        ollama_json_t *item = parse_value(ps);
        if (!item || !ollama_json_append(arr, item)) break;
        skip_ws(ps);
        if (ps->p >= ps->end) break;
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == ']') {
            ps->p++;
            return arr;
        }
        break;
    }
    ollama_json_free(arr);
    return NULL;
}

static ollama_json_t *parse_object(parser_t *ps) {
    ollama_json_t *obj = ollama_json_object();
    if (!obj) return NULL;
    ps->p++; /* { */
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == '}') {
        ps->p++;
        return obj;
    }
    for (;;) {
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != '"') break;
        size_t keylen;
        char *key = parse_string(ps, &keylen);
        if (!key) break;
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':') {
            free(key);
            break;
        }
        ps->p++;
        ollama_json_t *value = parse_value(ps);
        if (!value) {
            free(key);
            break;
        }
        if (!object_push(obj, key, value)) break;
        skip_ws(ps);
        if (ps->p >= ps->end) break;
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == '}') {
            ps->p++;
            return obj;
        }
        break;
    }
    ollama_json_free(obj);
    return NULL;
}

static ollama_json_t *parse_value(parser_t *ps) {
    skip_ws(ps);
    if (ps->p >= ps->end || ps->depth >= MAX_DEPTH) return NULL;
    ollama_json_t *v = NULL;
    ps->depth++;
    switch (*ps->p) {
    case '{': v = parse_object(ps); break;
    case '[': v = parse_array(ps); break;
    case '"': {
        size_t len;
        char *s = parse_string(ps, &len);
        v = s ? string_take(s, len) : NULL;
        break;
    }
    case 't': v = literal(ps, "true") ? ollama_json_bool(true) : NULL; break;
    case 'f': v = literal(ps, "false") ? ollama_json_bool(false) : NULL; break;
    case 'n': v = literal(ps, "null") ? ollama_json_null() : NULL; break;
    default: v = parse_number(ps); break;
    }
    ps->depth--;
    return v;
}

ollama_json_t *json_parse_n(const char *text, size_t len) {
    if (!text) return NULL;
    parser_t ps = {text, text + len, 0};
    ollama_json_t *v = parse_value(&ps);
    skip_ws(&ps);
    if (v && ps.p != ps.end) {
        ollama_json_free(v);
        return NULL;
    }
    return v;
}

ollama_json_t *ollama_json_parse(const char *text) { return text ? json_parse_n(text, strlen(text)) : NULL; }
