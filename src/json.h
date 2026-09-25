#ifndef JEFETCH_JSON_H
#define JEFETCH_JSON_H

#include <stddef.h>

typedef enum {
    JV_NULL,
    JV_BOOL,
    JV_INT,
    JV_UINT,
    JV_FLOAT,
    JV_STR,
    JV_ARR,
    JV_OBJ
} JvType;

typedef struct JsonValue JsonValue;
typedef struct {
    char *key;
    JsonValue *val;
} JvMember;

struct JsonValue {
    JvType type;
    int boolean;
    long long i;
    unsigned long long u;
    double f;
    char *str;
    JsonValue **items;
    size_t n;
    JvMember *members;
    size_t nm;
};

JsonValue *json_parse(const char *text, char *err, size_t errn);
JsonValue *json_clone(const JsonValue *j);
JsonValue *json_new_null(void);
JsonValue *json_new_bool(int b);
JsonValue *json_new_int(long long v);
JsonValue *json_new_uint(unsigned long long v);
JsonValue *json_new_float(double v);
JsonValue *json_new_str(const char *s);
JsonValue *json_new_arr(void);
JsonValue *json_new_obj(void);
void json_arr_push(JsonValue *a, JsonValue *v);
void json_obj_put(JsonValue *o, const char *k, JsonValue *v);
void json_free(JsonValue *j);
const JsonValue *json_get(const JsonValue *j, const char *key);
const char *json_str(const JsonValue *j);
int json_bool(const JsonValue *j, int *out);
int json_u64(const JsonValue *j, unsigned long long *out);
int json_i64(const JsonValue *j, long long *out);
int json_f64(const JsonValue *j, double *out);
int json_is_null(const JsonValue *j);
size_t json_arr_len(const JsonValue *j);
const JsonValue *json_arr_get(const JsonValue *j, size_t i);
size_t json_obj_len(const JsonValue *j);
const char *json_obj_key(const JsonValue *j, size_t i);
const JsonValue *json_obj_val(const JsonValue *j, size_t i);
void json_write(const JsonValue *j, char **out, size_t *len);
void json_write_pretty(const JsonValue *j, char **out, size_t *len);

#endif
