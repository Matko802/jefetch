#ifndef JEFETCH_CONFIG_H
#define JEFETCH_CONFIG_H

#include <stddef.h>

#include "json.h"

extern const char *JF_DEFAULT_JSONC_CONFIG;

typedef struct {
    char *line;
    char *color;
} ColorMapEntry;

typedef struct {
    char *logo_type;
    char *source;
    char *color;
    ColorMapEntry *color_map;
    size_t ncolor_map;
    int has_pad_top;
    size_t padding_top;
    int has_pad_left;
    size_t padding_left;
    int has_pad_right;
    size_t padding_right;
    int has_font_size;
    unsigned font_size;
    char *logo_key;
    int has_width;
    unsigned width;
    int has_height;
    unsigned height;
    char *animation;
    char *style;
    char *chars;
    char *sharkvis;
} LogoConfig;

typedef struct {
    char *separator;
    char *separator_color;
    char *key_color;
    char *title_color;
    size_t key_width;
    int key_width_right_aligned;
    size_t padding;
    char *bar_border_left;
    char *bar_border_right;
    char *bar_char_elapsed;
    char *bar_char_total;
    size_t bar_width;
    unsigned percent_type;
    unsigned colors[15];
    char *pipe;
    int bright_color;
    int color_align;
    int hide_cursor;
    int is_smart;
    int text_live;
} DisplayConfig;

typedef struct {
    char *module_overflow;
    int processes;
    unsigned thread_timeout;
    int add_nested_block;
    int error_output;
    int disable_linewrap;
    int show_hidden;
    char *file_icon;
    char *file_style;
    unsigned file_color;
    char *file_size;
    char *file_lines;
    unsigned file_max_pkg_count;
    char *player;
    unsigned net_dns_worker;
    unsigned wm_worker;
    char *custom_cpu_name;
    char *custom_temperature_name;
    unsigned num_fonts;
    unsigned font_worker;
    int font_prefer_mirror;
    int logo_title_key;
    char **video_decoders;
    size_t nvideo_decoders;
    char **video_encoders;
    size_t nvideo_encoders;
    int use_a2fa;
    char *target_path;
    int lazy_packages;
} GeneralConfig;

typedef struct {
    char *key;
    char *key_color;
    char *format;
    char *prefix;
    int hide_if_empty;
    int hide_if_not_supported;
    char *output_color;
    int has_output_custom_color;
    unsigned output_custom_color;
    int title;
    char *type;
    int has_fmt;
} ModuleArgs;

typedef struct {
    int is_object;
    char *name;
    char *module;
    ModuleArgs args;
    JsonValue *raw;
} ModuleEntry;

typedef struct {
    char *key;
    JsonValue *val;
} ModuleOption;

typedef struct {
    LogoConfig logo;
    DisplayConfig display;
    GeneralConfig general;
    ModuleEntry *modules;
    size_t nmodules;
    char *loaded_from;
    ModuleOption *options;
    size_t noptions;
} JfConfig;

void config_default(JfConfig *c);
int config_from_jsonc(JfConfig *c, const char *text, char *err, size_t errn);
int config_from_json_value(JfConfig *c, const JsonValue *root, char *err, size_t errn);
void config_free(JfConfig *c);
void config_clone(JfConfig *dst, const JfConfig *src);
const JsonValue *config_module_options(const JfConfig *c, const char *module);
const char *module_entry_name(const ModuleEntry *e);

#endif
