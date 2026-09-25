#include <strings.h>

#include "logo.h"

static int name_eq(const char *a, const char *b) {
    return strcasecmp(a, b) == 0;
}

const JfLogo *logo_by_name(const char *name) {
    for (size_t i = 0; i < JF_LOGO_COUNT; i++) {
        if (name_eq(JF_LOGOS[i].name, name))
            return &JF_LOGOS[i];
        for (size_t k = 0; k < JF_LOGOS[i].naliases; k++) {
            if (name_eq(JF_LOGOS[i].aliases[k], name))
                return &JF_LOGOS[i];
        }
    }
    return NULL;
}

const char *logo_name_at(size_t i) {
    if (i >= JF_LOGO_COUNT)
        return NULL;
    return JF_LOGOS[i].name;
}
