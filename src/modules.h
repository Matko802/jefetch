#ifndef JEFETCH_MODULES_H
#define JEFETCH_MODULES_H

#include <stddef.h>

#include "config.h"
#include "json.h"

typedef struct {
    const char *name;
    const char *desc;
    int has_format;
} ModuleInfo;

extern const ModuleInfo JF_MODULES[];
extern const size_t JF_NMODULES;

const ModuleInfo *module_from_name(const char *name);

typedef struct {
    char *key;
    char **values;
    size_t nvalues;
    int supported;
    int blank;
    int repeat_key;
    char **per_value_keys;
    size_t nper;
} ModuleOutput;

void module_output_free_contents(ModuleOutput *o);

typedef struct {
    char *module;
    ModuleArgs args;
    JsonValue *raw;
} ModuleInstance;

void module_instance_init(ModuleInstance *inst, const ModuleEntry *e);
void module_instance_free(ModuleInstance *inst);

ModuleOutput *module_run_instance(const ModuleInstance *inst, const JfConfig *cfg);
JsonValue *module_json_result(const char *name, const ModuleInstance *inst,
                              const JfConfig *cfg);
const char *module_json_type_name(const char *name);
char *module_json_error(const char *name, const ModuleInstance *inst,
                        const JfConfig *cfg);
int module_packages_owns_format(const char *fmt);

#endif
