#ifndef JEFETCH_APP_H
#define JEFETCH_APP_H

#include <stddef.h>

#include "anim.h"
#include "config.h"
#include "logo_image.h"

typedef struct {
    char *structure;
    char **structure_disabled;
    size_t ndisabled;
    char *config_path;
    int no_config;
    int json;
    int force_static;
    char *logo_name;
} CliOptions;

typedef struct {
    CliOptions options;
    JfConfig config;
    ResolvedLogo *logo;
} App;

void app_init(App *app);
void app_free(App *app);
void app_load_config(App *app);
char *app_ensure_default_config(App *app);
int app_run(App *app);

char **app_config_search_dirs(size_t *n);
void app_free_strs(char **p, size_t n);
int app_stdout_is_tty(void);

typedef enum {
    KEY_QUIT,
    KEY_TOGGLE,
    KEY_IGNORE
} KeyAction;

KeyAction app_classify_key(unsigned char b);

#endif
