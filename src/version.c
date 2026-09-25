#include <stdio.h>

#include "version.h"

const char *JF_RAW_VERSION = "0.1.1";
const char *JF_COMPILED_ON = "2026-09-01";
const char *JF_BUILD_TYPE = "Release";

#ifndef JEFETCH_TARGET
#define JEFETCH_TARGET "unknown"
#endif
#ifndef JEFETCH_LIB
#define JEFETCH_LIB "unknown"
#endif

void jf_print_full(void) {
    printf("jefetch %s\n", JF_RAW_VERSION);
    printf("Compiled on: %s\n", JF_COMPILED_ON);
    printf("Build type: %s\n", JF_BUILD_TYPE);
    printf("Compile target: %s\n", JEFETCH_TARGET);
    printf("System lib: %s\n", JEFETCH_LIB);
    printf("Compressed: No\n");
    printf("Features: None\n");
}
