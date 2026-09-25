#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

typedef struct {
    const uint8_t *b;
    size_t len;
    size_t pos;
    char err[256];
    int failed;
} Parser;

static JsonValue *jnew(int type) {
    JsonValue *j = calloc(1, sizeof *j);
    if (j)
        j->type = type;
    return j;
}

void json_free(JsonValue *j) {
    size_t i;
    if (!j)
        return;
    free(j->str);
    for (i = 0; i < j->n; i++)
        json_free(j->items[i]);
    free(j->items);
    for (i = 0; i < j->nm; i++) {
        free(j->members[i].key);
        json_free(j->members[i].val);
    }
    free(j->members);
    free(j);
}

const JsonValue *json_get(const JsonValue *j, const char *key) {
    size_t i;
    if (!j || j->type != JV_OBJ)
        return NULL;
    for (i = 0; i < j->nm; i++) {
        if (!strcmp(j->members[i].key, key))
            return j->members[i].val;
    }
    return NULL;
}

const char *json_str(const JsonValue *j) {
    return (j && j->type == JV_STR) ? j->str : NULL;
}

int json_bool(const JsonValue *j, int *out) {
    if (!j || j->type != JV_BOOL)
        return 0;
    *out = j->boolean;
    return 1;
}

int json_u64(const JsonValue *j, unsigned long long *out) {
    if (!j)
        return 0;
    if (j->type == JV_UINT) {
        *out = j->u;
        return 1;
    }
    if (j->type == JV_INT) {
        *out = (unsigned long long)j->i;
        return 1;
    }
    return 0;
}

int json_i64(const JsonValue *j, long long *out) {
    if (!j)
        return 0;
    if (j->type == JV_INT) {
        *out = j->i;
        return 1;
    }
    if (j->type == JV_UINT) {
        *out = (long long)j->u;
        return 1;
    }
    return 0;
}

int json_f64(const JsonValue *j, double *out) {
    if (!j)
        return 0;
    if (j->type == JV_FLOAT) {
        *out = j->f;
        return 1;
    }
    if (j->type == JV_INT) {
        *out = (double)j->i;
        return 1;
    }
    if (j->type == JV_UINT) {
        *out = (double)j->u;
        return 1;
    }
    return 0;
}

int json_is_null(const JsonValue *j) {
    return j && j->type == JV_NULL;
}

size_t json_arr_len(const JsonValue *j) {
    return (j && j->type == JV_ARR) ? j->n : 0;
}

const JsonValue *json_arr_get(const JsonValue *j, size_t i) {
    if (!j || j->type != JV_ARR || i >= j->n)
        return NULL;
    return j->items[i];
}

size_t json_obj_len(const JsonValue *j) {
    return (j && j->type == JV_OBJ) ? j->nm : 0;
}

const char *json_obj_key(const JsonValue *j, size_t i) {
    if (!j || j->type != JV_OBJ || i >= j->nm)
        return NULL;
    return j->members[i].key;
}

const JsonValue *json_obj_val(const JsonValue *j, size_t i) {
    if (!j || j->type != JV_OBJ || i >= j->nm)
        return NULL;
    return j->members[i].val;
}

static void wput(char **out, size_t *len, const char *s, size_t n) {
    memcpy(*out + *len, s, n);
    *len += n;
}

static void wgrow(char **out, size_t *cap, size_t need) {
    if (need <= *cap)
        return;
    size_t c = *cap ? *cap : 256;
    while (c < need)
        c *= 2;
    *out = realloc(*out, c);
    *cap = c;
}

static void wpad(int indent, char **out, size_t *len, size_t *cap) {
    int i;
    wgrow(out, cap, *len + (size_t)indent * 2 + 1);
    for (i = 0; i < indent; i++)
        wput(out, len, "  ", 2);
}

static void wesc(const char *s, char **out, size_t *len, size_t *cap, int pretty) {
    (void)pretty;
    const unsigned char *p = (const unsigned char *)s;
    wgrow(out, cap, *len + strlen(s) * 6 + 3);
    wput(out, len, "\"", 1);
    while (*p) {
        if (*p == '"')
            wput(out, len, "\\\"", 2);
        else if (*p == '\\')
            wput(out, len, "\\\\", 2);
        else if (*p == '\n')
            wput(out, len, "\\n", 2);
        else if (*p == '\r')
            wput(out, len, "\\r", 2);
        else if (*p == '\t')
            wput(out, len, "\\t", 2);
        else if (*p < 0x20) {
            char tmp[8];
            int n = snprintf(tmp, sizeof tmp, "\\u%04x", *p);
            wput(out, len, tmp, (size_t)n);
        } else {
            wput(out, len, (const char *)p, 1);
        }
        p++;
    }
    wput(out, len, "\"", 1);
}

static void wval(const JsonValue *j, int indent, int pretty, char **out, size_t *len,
                 size_t *cap);

static void wpretty_arr(const JsonValue *j, int indent, char **out, size_t *len,
                        size_t *cap) {
    size_t i;
    if (j->n == 0) {
        wgrow(out, cap, *len + 3);
        wput(out, len, "[]", 2);
        return;
    }
    wgrow(out, cap, *len + 3);
    wput(out, len, "[\n", 2);
    for (i = 0; i < j->n; i++) {
        wpad(indent + 1, out, len, cap);
        wval(j->items[i], indent + 1, 1, out, len, cap);
        if (i + 1 < j->n)
            wput(out, len, ",", 1);
        wput(out, len, "\n", 1);
    }
    wpad(indent, out, len, cap);
    wput(out, len, "]", 1);
}

static void wpretty_obj(const JsonValue *j, int indent, char **out, size_t *len,
                        size_t *cap) {
    size_t i;
    if (j->nm == 0) {
        wgrow(out, cap, *len + 3);
        wput(out, len, "{}", 2);
        return;
    }
    wgrow(out, cap, *len + 3);
    wput(out, len, "{\n", 2);
    for (i = 0; i < j->nm; i++) {
        wpad(indent + 1, out, len, cap);
        wesc(j->members[i].key, out, len, cap, 1);
        wput(out, len, ": ", 2);
        wval(j->members[i].val, indent + 1, 1, out, len, cap);
        if (i + 1 < j->nm)
            wput(out, len, ",", 1);
        wput(out, len, "\n", 1);
    }
    wpad(indent, out, len, cap);
    wput(out, len, "}", 1);
}

static void wval(const JsonValue *j, int indent, int pretty, char **out, size_t *len,
                 size_t *cap) {
    size_t i;
    char tmp[64];
    if (pretty && j->type == JV_ARR) {
        wpretty_arr(j, indent, out, len, cap);
        return;
    }
    if (pretty && j->type == JV_OBJ) {
        wpretty_obj(j, indent, out, len, cap);
        return;
    }
    switch (j->type) {
    case JV_NULL:
        wgrow(out, cap, *len + 5);
        wput(out, len, "null", 4);
        break;
    case JV_BOOL:
        wgrow(out, cap, *len + 6);
        if (j->boolean)
            wput(out, len, "true", 4);
        else
            wput(out, len, "false", 5);
        break;
    case JV_INT:
        snprintf(tmp, sizeof tmp, "%lld", j->i);
        wgrow(out, cap, *len + strlen(tmp) + 1);
        wput(out, len, tmp, strlen(tmp));
        break;
    case JV_UINT:
        snprintf(tmp, sizeof tmp, "%llu", j->u);
        wgrow(out, cap, *len + strlen(tmp) + 1);
        wput(out, len, tmp, strlen(tmp));
        break;
    case JV_FLOAT:
        snprintf(tmp, sizeof tmp, "%g", j->f);
        wgrow(out, cap, *len + strlen(tmp) + 1);
        wput(out, len, tmp, strlen(tmp));
        break;
    case JV_STR:
        wesc(j->str, out, len, cap, pretty);
        break;
    case JV_ARR:
        wgrow(out, cap, *len + 2);
        wput(out, len, "[", 1);
        for (i = 0; i < j->n; i++) {
            if (i > 0)
                wput(out, len, ",", 1);
            wval(j->items[i], indent, 0, out, len, cap);
        }
        wput(out, len, "]", 1);
        break;
    case JV_OBJ:
        wgrow(out, cap, *len + 2);
        wput(out, len, "{", 1);
        for (i = 0; i < j->nm; i++) {
            if (i > 0)
                wput(out, len, ",", 1);
            wesc(j->members[i].key, out, len, cap, 0);
            wput(out, len, ":", 1);
            wval(j->members[i].val, indent, 0, out, len, cap);
        }
        wput(out, len, "}", 1);
        break;
    }
}

void json_write(const JsonValue *j, char **out, size_t *len) {
    size_t cap = 256;
    *out = malloc(cap);
    *len = 0;
    wval(j, 0, 0, out, len, &cap);
    (*out)[*len] = 0;
}

void json_write_pretty(const JsonValue *j, char **out, size_t *len) {
    size_t cap = 256;
    *out = malloc(cap);
    *len = 0;
    wval(j, 0, 1, out, len, &cap);
    (*out)[*len] = 0;
}

static int p_peek(Parser *p) {
    return p->pos < p->len ? p->b[p->pos] : -1;
}

static void p_skip_ws(Parser *p) {
    int c;
    while ((c = p_peek(p)) == ' ' || c == '\t' || c == '\n' || c == '\r')
        p->pos++;
}

static void p_skip_ws_comments(Parser *p) {
    for (;;) {
        p_skip_ws(p);
        if (p->pos + 1 < p->len && p->b[p->pos] == '/' && p->b[p->pos + 1] == '/') {
            while (p->pos < p->len && p->b[p->pos] != '\n')
                p->pos++;
        } else if (p->pos + 1 < p->len && p->b[p->pos] == '/' && p->b[p->pos + 1] == '*') {
            p->pos += 2;
            for (;;) {
                if (p->pos + 1 >= p->len)
                    break;
                if (p->b[p->pos] == '*' && p->b[p->pos + 1] == '/') {
                    p->pos += 2;
                    break;
                }
                p->pos++;
            }
        } else {
            return;
        }
    }
}

static JsonValue *p_value(Parser *p);

static int p_hex4(Parser *p, unsigned *out) {
    unsigned v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        int c;
        if (p->pos >= p->len)
            return 0;
        c = p->b[p->pos++];
        if (c >= '0' && c <= '9')
            v = v * 16 + (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v = v * 16 + (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v = v * 16 + (unsigned)(c - 'A' + 10);
        else
            return 0;
    }
    *out = v;
    return 1;
}

static void utf8_push(char **o, size_t *n, size_t *cap, unsigned cp) {
    while (*n + 5 > *cap) {
        *cap *= 2;
        *o = realloc(*o, *cap);
    }
    if (cp < 0x80) {
        (*o)[(*n)++] = (char)cp;
    } else if (cp < 0x800) {
        (*o)[(*n)++] = (char)(0xC0 | (cp >> 6));
        (*o)[(*n)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        (*o)[(*n)++] = (char)(0xE0 | (cp >> 12));
        (*o)[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        (*o)[(*n)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        (*o)[(*n)++] = (char)(0xF0 | (cp >> 18));
        (*o)[(*n)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        (*o)[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        (*o)[(*n)++] = (char)(0x80 | (cp & 0x3F));
    }
}

static char *p_string(Parser *p) {
    size_t cap = 64, n = 0;
    char *o = malloc(cap);
    p->pos++;
    for (;;) {
        int c;
        if (p->pos >= p->len) {
            free(o);
            return NULL;
        }
        c = p->b[p->pos++];
        if (c == '"')
            break;
        if (c == '\\') {
            unsigned cp;
            int e;
            if (p->pos >= p->len) {
                free(o);
                return NULL;
            }
            e = p->b[p->pos++];
            if (e == '"')
                utf8_push(&o, &n, &cap, '"');
            else if (e == '\\')
                utf8_push(&o, &n, &cap, '\\');
            else if (e == '/')
                utf8_push(&o, &n, &cap, '/');
            else if (e == 'b')
                utf8_push(&o, &n, &cap, 8);
            else if (e == 'f')
                utf8_push(&o, &n, &cap, 12);
            else if (e == 'n')
                utf8_push(&o, &n, &cap, '\n');
            else if (e == 'r')
                utf8_push(&o, &n, &cap, '\r');
            else if (e == 't')
                utf8_push(&o, &n, &cap, '\t');
            else if (e == 'u') {
                if (!p_hex4(p, &cp)) {
                    free(o);
                    return NULL;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (p->pos + 1 < p->len && p->b[p->pos] == '\\' && p->b[p->pos + 1] == 'u') {
                        unsigned low;
                        p->pos += 2;
                        if (!p_hex4(p, &low)) {
                            free(o);
                            return NULL;
                        }
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            utf8_push(&o, &n, &cap, 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00));
                        } else {
                            utf8_push(&o, &n, &cap, 0xFFFD);
                            utf8_push(&o, &n, &cap, low <= 0x10FFFF ? low : 0xFFFD);
                        }
                    } else {
                        utf8_push(&o, &n, &cap, 0xFFFD);
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    utf8_push(&o, &n, &cap, 0xFFFD);
                } else {
                    utf8_push(&o, &n, &cap, cp);
                }
            } else {
                free(o);
                return NULL;
            }
        } else {
            size_t k;
            unsigned char f = (unsigned char)c;
            if (f < 0x80)
                k = 1;
            else if ((f & 0xE0) == 0xC0)
                k = 2;
            else if ((f & 0xF0) == 0xE0)
                k = 3;
            else
                k = 4;
            while (n + k + 1 > cap) {
                cap *= 2;
                o = realloc(o, cap);
            }
            if (p->pos - 1 + k > p->len) {
                utf8_push(&o, &n, &cap, 0xFFFD);
            } else {
                memcpy(o + n, p->b + p->pos - 1, k);
                n += k;
                p->pos += k - 1;
            }
        }
    }
    while (n + 1 > cap) {
        cap *= 2;
        o = realloc(o, cap);
    }
    o[n] = 0;
    return o;
}

static JsonValue *p_number(Parser *p) {
    size_t start = p->pos;
    int is_float = 0;
    p->pos++;
    for (;;) {
        int c = p_peek(p);
        if (c >= '0' && c <= '9') {
            p->pos++;
        } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            is_float = 1;
            p->pos++;
        } else {
            break;
        }
    }
    size_t n = p->pos - start;
    char *tmp = malloc(n + 1);
    memcpy(tmp, p->b + start, n);
    tmp[n] = 0;
    JsonValue *j;
    if (is_float) {
        char *end;
        double f = strtod(tmp, &end);
        if (end == tmp) {
            free(tmp);
            return NULL;
        }
        j = jnew(JV_FLOAT);
        j->f = f;
    } else if (tmp[0] == '-') {
        char *end;
        long long v = strtoll(tmp, &end, 10);
        if (end != tmp && *end == 0) {
            j = jnew(JV_INT);
            j->i = v;
        } else {
            double f = strtod(tmp, &end);
            if (end == tmp) {
                free(tmp);
                return NULL;
            }
            j = jnew(JV_FLOAT);
            j->f = f;
        }
    } else {
        char *end;
        unsigned long long v = strtoull(tmp, &end, 10);
        if (end != tmp && *end == 0) {
            j = jnew(JV_UINT);
            j->u = v;
        } else {
            double f = strtod(tmp, &end);
            if (end == tmp) {
                free(tmp);
                return NULL;
            }
            j = jnew(JV_FLOAT);
            j->f = f;
        }
    }
    free(tmp);
    return j;
}

static JsonValue *p_value(Parser *p) {
    p_skip_ws_comments(p);
    int c = p_peek(p);
    if (c < 0) {
        snprintf(p->err, sizeof p->err, "unexpected end of input");
        p->failed = 1;
        return NULL;
    }
    if (c == '{') {
        JsonValue *j = jnew(JV_OBJ);
        p->pos++;
        for (;;) {
            p_skip_ws_comments(p);
            c = p_peek(p);
            if (c == '}') {
                p->pos++;
                return j;
            }
            if (c != '"') {
                snprintf(p->err, sizeof p->err, "expected object key at byte %zu", p->pos);
                p->failed = 1;
                json_free(j);
                return NULL;
            }
            char *key = p_string(p);
            if (!key) {
                p->failed = 1;
                json_free(j);
                return NULL;
            }
            p_skip_ws_comments(p);
            if (p_peek(p) != ':') {
                free(key);
                p->failed = 1;
                json_free(j);
                return NULL;
            }
            p->pos++;
            JsonValue *v = p_value(p);
            if (!v) {
                free(key);
                json_free(j);
                return NULL;
            }
            j->members = realloc(j->members, (j->nm + 1) * sizeof(JvMember));
            j->members[j->nm].key = key;
            j->members[j->nm].val = v;
            j->nm++;
            p_skip_ws_comments(p);
            c = p_peek(p);
            if (c == ',') {
                p->pos++;
            } else if (c == '}') {
                p->pos++;
                return j;
            } else {
                p->failed = 1;
                json_free(j);
                return NULL;
            }
        }
    }
    if (c == '[') {
        JsonValue *j = jnew(JV_ARR);
        p->pos++;
        for (;;) {
            p_skip_ws_comments(p);
            c = p_peek(p);
            if (c == ']') {
                p->pos++;
                return j;
            }
            if (c < 0) {
                p->failed = 1;
                json_free(j);
                return NULL;
            }
            JsonValue *v = p_value(p);
            if (!v) {
                json_free(j);
                return NULL;
            }
            j->items = realloc(j->items, (j->n + 1) * sizeof(JsonValue *));
            j->items[j->n++] = v;
            p_skip_ws_comments(p);
            c = p_peek(p);
            if (c == ',') {
                p->pos++;
            } else if (c == ']') {
                p->pos++;
                return j;
            } else {
                p->failed = 1;
                json_free(j);
                return NULL;
            }
        }
    }
    if (c == '"') {
        char *s = p_string(p);
        if (!s) {
            p->failed = 1;
            return NULL;
        }
        JsonValue *j = jnew(JV_STR);
        j->str = s;
        return j;
    }
    if (c == 't' && p->pos + 4 <= p->len && !memcmp(p->b + p->pos, "true", 4)) {
        p->pos += 4;
        JsonValue *j = jnew(JV_BOOL);
        j->boolean = 1;
        return j;
    }
    if (c == 'f' && p->pos + 5 <= p->len && !memcmp(p->b + p->pos, "false", 5)) {
        p->pos += 5;
        JsonValue *j = jnew(JV_BOOL);
        j->boolean = 0;
        return j;
    }
    if (c == 'n' && p->pos + 4 <= p->len && !memcmp(p->b + p->pos, "null", 4)) {
        p->pos += 4;
        return jnew(JV_NULL);
    }
    if (c == '-' || (c >= '0' && c <= '9'))
        return p_number(p);
    snprintf(p->err, sizeof p->err, "unexpected character at byte %zu", p->pos);
    p->failed = 1;
    return NULL;
}

JsonValue *json_clone(const JsonValue *j) {
    size_t i;
    JsonValue *c;
    if (!j)
        return NULL;
    c = jnew(j->type);
    if (!c)
        return NULL;
    c->boolean = j->boolean;
    c->i = j->i;
    c->u = j->u;
    c->f = j->f;
    if (j->str)
        c->str = strdup(j->str);
    for (i = 0; i < j->n; i++) {
        JsonValue *k = json_clone(j->items[i]);
        c->items = realloc(c->items, (c->n + 1) * sizeof(JsonValue *));
        c->items[c->n++] = k;
    }
    for (i = 0; i < j->nm; i++) {
        JsonValue *k = json_clone(j->members[i].val);
        c->members = realloc(c->members, (c->nm + 1) * sizeof(JvMember));
        c->members[c->nm].key = strdup(j->members[i].key);
        c->members[c->nm].val = k;
        c->nm++;
    }
    return c;
}

JsonValue *json_parse(const char *text, char *err, size_t errn) {
    Parser p;
    p.b = (const uint8_t *)text;
    p.len = strlen(text);
    p.pos = 0;
    p.failed = 0;
    p.err[0] = 0;
    p_skip_ws_comments(&p);
    JsonValue *v = p_value(&p);
    if (!v) {
        snprintf(err, errn, "%s", p.err[0] ? p.err : "parse error");
        return NULL;
    }
    p_skip_ws_comments(&p);
    if (p.pos != p.len) {
        snprintf(err, errn, "unexpected trailing data at byte %zu", p.pos);
        json_free(v);
        return NULL;
    }
    return v;
}

JsonValue *json_new_null(void) {
    return jnew(JV_NULL);
}

JsonValue *json_new_bool(int b) {
    JsonValue *j = jnew(JV_BOOL);
    if (j)
        j->boolean = b ? 1 : 0;
    return j;
}

JsonValue *json_new_int(long long v) {
    JsonValue *j = jnew(JV_INT);
    if (j)
        j->i = v;
    return j;
}

JsonValue *json_new_uint(unsigned long long v) {
    JsonValue *j = jnew(JV_UINT);
    if (j)
        j->u = v;
    return j;
}

JsonValue *json_new_float(double v) {
    JsonValue *j = jnew(JV_FLOAT);
    if (j)
        j->f = v;
    return j;
}

JsonValue *json_new_str(const char *s) {
    JsonValue *j = jnew(JV_STR);
    if (j)
        j->str = strdup(s ? s : "");
    return j;
}

JsonValue *json_new_arr(void) {
    return jnew(JV_ARR);
}

JsonValue *json_new_obj(void) {
    return jnew(JV_OBJ);
}

void json_arr_push(JsonValue *a, JsonValue *v) {
    if (!a || a->type != JV_ARR)
        return;
    a->items = realloc(a->items, (a->n + 1) * sizeof(JsonValue *));
    a->items[a->n++] = v;
}

void json_obj_put(JsonValue *o, const char *k, JsonValue *v) {
    if (!o || o->type != JV_OBJ)
        return;
    o->members = realloc(o->members, (o->nm + 1) * sizeof(JvMember));
    o->members[o->nm].key = strdup(k);
    o->members[o->nm].val = v;
    o->nm++;
}
