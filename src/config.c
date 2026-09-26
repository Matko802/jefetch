#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

const char *JF_DEFAULT_JSONC_CONFIG =
    "{\n"
    "    \"modules\": [\n"
    "        \"title\",\n"
    "        \"separator\",\n"
    "        \"os\",\n"
    "        \"host\",\n"
    "        \"kernel\",\n"
    "        \"uptime\",\n"
    "        { \"type\": \"packages\", \"combined\": true },\n"
    "        \"shell\",\n"
    "        \"display\",\n"
    "        \"wm\",\n"
    "        \"theme\",\n"
    "        \"icons\",\n"
    "        \"font\",\n"
    "        \"cursor\",\n"
    "        \"terminal\",\n"
    "        \"cpu\",\n"
    "        \"gpu\",\n"
    "        \"memory\",\n"
    "        \"swap\",\n"
    "        \"disk\",\n"
    "        \"localip\",\n"
    "        \"locale\",\n"
    "        \"break\",\n"
    "        \"colors\",\n"
    "    ],\n"
    "    \"display\": {\n"
    "        \"separator\": \"->\",\n"
    "        \"separatorColor\": \"red\",\n"
    "        \"keyColor\": \"\",\n"
    "        \"titleColor\": \"\",\n"
    "        \"padding\": 1,\n"
    "        \"brightColor\": true\n"
    "    },\n"
    "    \"logo\": {\n"
    "        \"source\": \"\",\n"
    "        \"animation\": \"speed=1 xy\",\n"
    "        \"sharkvis\": \"xzy return=10 chars=blocks color=sharkvis\",\n"
    "        \"padding\": {\n"
    "            \"top\": 0,\n"
    "            \"left\": 0,\n"
    "            \"right\": 0\n"
    "        }\n"
    "    }\n"
    "}\n";

static int is_sharkvis_name(const char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        n--;
    if (n != 8)
        return 0;
    static const char *t = "sharkvis";
    for (size_t i = 0; i < 8; i++) {
        char a = s[i];
        if (a >= 'A' && a <= 'Z')
            a += 32;
        if (a != t[i])
            return 0;
    }
    return 1;
}

static void logo_default(LogoConfig *l) {
    memset(l, 0, sizeof *l);
}

static void display_default(DisplayConfig *d) {
    static const unsigned cols[15] = {41, 42, 43, 44, 45, 46, 47,
                                      100, 101, 102, 103, 104, 105, 106, 107};
    memset(d, 0, sizeof *d);
    d->separator = strdup(": ");
    d->key_width = 0;
    d->padding = 0;
    d->bar_border_left = strdup("[");
    d->bar_border_right = strdup("]");
    d->bar_char_elapsed = strdup("-");
    d->bar_char_total = strdup("-");
    d->bar_width = 20;
    d->percent_type = 0;
    memcpy(d->colors, cols, sizeof cols);
    d->bright_color = 1;
    d->color_align = 1;
    d->hide_cursor = 1;
}

static void general_default(GeneralConfig *g) {
    memset(g, 0, sizeof *g);
    g->module_overflow = strdup("");
    g->file_icon = strdup("");
    g->file_style = strdup("");
    g->file_size = strdup("");
    g->file_lines = strdup("");
    g->player = strdup("");
    g->custom_cpu_name = strdup("");
    g->custom_temperature_name = strdup("");
    g->target_path = strdup("");
}

void config_default(JfConfig *c) {
    memset(c, 0, sizeof *c);
    logo_default(&c->logo);
    display_default(&c->display);
    general_default(&c->general);
}

static void logo_parse(LogoConfig *l, const JsonValue *root) {
    const JsonValue *o = json_get(root, "logo");
    if (!o || o->type != JV_OBJ)
        goto fallback;
    for (size_t i = 0; i < o->nm; i++) {
        const char *k = o->members[i].key;
        const JsonValue *v = o->members[i].val;
        const char *s = json_str(v);
        if (!strcmp(k, "type")) {
            if (s)
                l->logo_type = strdup(s);
        } else if (!strcmp(k, "source")) {
            if (s) {
                l->source = strdup(s);
            } else {
                char *js = NULL;
                size_t jl = 0;
                json_write(v, &js, &jl);
                l->source = js;
            }
        } else if (!strcmp(k, "color")) {
            if (s) {
                l->color = strdup(s);
            } else if (v->type == JV_OBJ) {
                for (size_t m = 0; m < v->nm; m++) {
                    const char *cn = json_str(v->members[m].val);
                    if (cn) {
                        l->color_map = realloc(l->color_map,
                                              (l->ncolor_map + 1) * sizeof(ColorMapEntry));
                        l->color_map[l->ncolor_map].line = strdup(v->members[m].key);
                        l->color_map[l->ncolor_map].color = strdup(cn);
                        l->ncolor_map++;
                    }
                }
            }
        } else if (!strcmp(k, "padding")) {
            if (v->type == JV_OBJ) {
                for (size_t m = 0; m < v->nm; m++) {
                    unsigned long long n = 0;
                    if (!json_u64(v->members[m].val, &n))
                        continue;
                    if (!strcmp(v->members[m].key, "top")) {
                        l->has_pad_top = 1;
                        l->padding_top = (size_t)n;
                    } else if (!strcmp(v->members[m].key, "left")) {
                        l->has_pad_left = 1;
                        l->padding_left = (size_t)n;
                    } else if (!strcmp(v->members[m].key, "right")) {
                        l->has_pad_right = 1;
                        l->padding_right = (size_t)n;
                    }
                }
            } else {
                unsigned long long n = 0;
                if (json_u64(v, &n)) {
                    l->has_pad_top = l->has_pad_left = l->has_pad_right = 1;
                    l->padding_top = l->padding_left = l->padding_right = (size_t)n;
                }
            }
        } else if (!strcmp(k, "fontSize")) {
            unsigned long long n = 0;
            if (json_u64(v, &n)) {
                l->has_font_size = 1;
                l->font_size = (unsigned)n;
            }
        } else if (!strcmp(k, "logoKey")) {
            if (s)
                l->logo_key = strdup(s);
        } else if (!strcmp(k, "width")) {
            unsigned long long n = 0;
            if (json_u64(v, &n)) {
                l->has_width = 1;
                l->width = (unsigned)n;
            }
        } else if (!strcmp(k, "height")) {
            unsigned long long n = 0;
            if (json_u64(v, &n)) {
                l->has_height = 1;
                l->height = (unsigned)n;
            }
        } else if (!strcmp(k, "animation")) {
            if (s)
                l->animation = strdup(s);
        } else if (!strcmp(k, "style") || !strcmp(k, "mode")) {
            if (s)
                l->style = strdup(s);
        } else if (!strcmp(k, "chars") || !strcmp(k, "characters")) {
            if (s)
                l->chars = strdup(s);
        } else if (!strcmp(k, "sharkvis")) {
            if (s) {
                l->sharkvis = strdup(s);
            } else {
                char *js = NULL;
                size_t jl = 0;
                json_write(v, &js, &jl);
                l->sharkvis = js;
            }
        }
    }
fallback:
    if (!l->animation) {
        const char *s = json_str(json_get(root, "animation"));
        if (s)
            l->animation = strdup(s);
    }
}

static void display_parse(DisplayConfig *d, const JsonValue *root) {
    const JsonValue *o = json_get(root, "display");
    if (!o || o->type != JV_OBJ)
        return;
    for (size_t i = 0; i < o->nm; i++) {
        const char *k = o->members[i].key;
        const JsonValue *v = o->members[i].val;
        const char *s = json_str(v);
        unsigned long long n = 0;
        int b = 0;
        if (!strcmp(k, "separator")) {
            if (s) {
                free(d->separator);
                d->separator = strdup(s);
            }
        } else if (!strcmp(k, "separatorColor")) {
            if (s) {
                free(d->separator_color);
                d->separator_color = is_sharkvis_name(s) ? NULL : strdup(s);
            }
        } else if (!strcmp(k, "keyColor")) {
            if (s) {
                free(d->key_color);
                d->key_color = is_sharkvis_name(s) ? NULL : strdup(s);
            }
        } else if (!strcmp(k, "titleColor")) {
            if (s) {
                free(d->title_color);
                d->title_color = is_sharkvis_name(s) ? NULL : strdup(s);
            }
        } else if (!strcmp(k, "keyWidth")) {
            d->key_width = json_u64(v, &n) ? (size_t)n : 0;
        } else if (!strcmp(k, "keyWidthRightAligned")) {
            if (json_bool(v, &b))
                d->key_width_right_aligned = b;
        } else if (!strcmp(k, "padding")) {
            d->padding = json_u64(v, &n) ? (size_t)n : 1;
        } else if (!strcmp(k, "barBorderLeft")) {
            if (s) {
                free(d->bar_border_left);
                d->bar_border_left = strdup(s);
            }
        } else if (!strcmp(k, "barBorderRight")) {
            if (s) {
                free(d->bar_border_right);
                d->bar_border_right = strdup(s);
            }
        } else if (!strcmp(k, "barCharElapsed")) {
            if (s) {
                free(d->bar_char_elapsed);
                d->bar_char_elapsed = strdup(s);
            }
        } else if (!strcmp(k, "barCharTotal")) {
            if (s) {
                free(d->bar_char_total);
                d->bar_char_total = strdup(s);
            }
        } else if (!strcmp(k, "barWidth")) {
            d->bar_width = json_u64(v, &n) ? (size_t)n : 20;
        } else if (!strcmp(k, "percentType")) {
            d->percent_type = json_u64(v, &n) ? (unsigned)n : 0;
        } else if (!strcmp(k, "pipe")) {
            if (s) {
                free(d->pipe);
                d->pipe = strdup(s);
            }
        } else if (!strcmp(k, "brightColor")) {
            if (json_bool(v, &b))
                d->bright_color = b;
        } else if (!strcmp(k, "colorAlign")) {
            if (json_bool(v, &b))
                d->color_align = b;
        } else if (!strcmp(k, "hideCursor")) {
            if (json_bool(v, &b))
                d->hide_cursor = b;
        } else if (!strcmp(k, "isSmart")) {
            if (json_bool(v, &b))
                d->is_smart = b;
        }
    }
}

static void general_parse(GeneralConfig *g, const JsonValue *root) {
    const JsonValue *o = json_get(root, "general");
    if (!o || o->type != JV_OBJ)
        return;
    for (size_t i = 0; i < o->nm; i++) {
        const char *k = o->members[i].key;
        const JsonValue *v = o->members[i].val;
        const char *s = json_str(v);
        unsigned long long n = 0;
        int b = 0;
        if (!strcmp(k, "moduleOverflow")) {
            if (s) {
                free(g->module_overflow);
                g->module_overflow = strdup(s);
            }
        } else if (!strcmp(k, "processes")) {
            if (json_bool(v, &b))
                g->processes = b;
        } else if (!strcmp(k, "threadTimeout")) {
            if (json_u64(v, &n))
                g->thread_timeout = (unsigned)n;
        } else if (!strcmp(k, "addNestedBlock")) {
            if (json_bool(v, &b))
                g->add_nested_block = b;
        } else if (!strcmp(k, "errorOutput")) {
            if (json_bool(v, &b))
                g->error_output = b;
        } else if (!strcmp(k, "disableLinewrap")) {
            if (json_bool(v, &b))
                g->disable_linewrap = b;
        } else if (!strcmp(k, "showHidden")) {
            if (json_bool(v, &b))
                g->show_hidden = b;
        } else if (!strcmp(k, "fileIcon")) {
            if (s) {
                free(g->file_icon);
                g->file_icon = strdup(s);
            }
        } else if (!strcmp(k, "player")) {
            if (s) {
                free(g->player);
                g->player = strdup(s);
            }
        } else if (!strcmp(k, "numFonts")) {
            if (json_u64(v, &n))
                g->num_fonts = (unsigned)n;
        } else if (!strcmp(k, "fontPreferMirror")) {
            if (json_bool(v, &b))
                g->font_prefer_mirror = b;
        } else if (!strcmp(k, "logoTitleKey")) {
            if (json_bool(v, &b))
                g->logo_title_key = b;
        } else if (!strcmp(k, "lazyPackages")) {
            if (json_bool(v, &b))
                g->lazy_packages = b;
        }
    }
}

static void moduleargs_parse(ModuleArgs *a, const JsonValue *obj) {
    const char *s;
    int b;
    s = json_str(json_get(obj, "key"));
    if (s)
        a->key = strdup(s);
    s = json_str(json_get(obj, "keyColor"));
    if (s && !is_sharkvis_name(s))
        a->key_color = strdup(s);
    s = json_str(json_get(obj, "format"));
    if (s) {
        a->format = strdup(s);
        a->has_fmt = 1;
    }
    s = json_str(json_get(obj, "prefix"));
    if (s)
        a->prefix = strdup(s);
    if (json_bool(json_get(obj, "hideIfEmpty"), &b))
        a->hide_if_empty = b;
    if (json_bool(json_get(obj, "hideIfNotSupported"), &b))
        a->hide_if_not_supported = b;
    s = json_str(json_get(obj, "outputColor"));
    if (s)
        a->output_color = strdup(s);
    s = json_str(json_get(obj, "type"));
    if (s)
        a->type = strdup(s);
}

static void entry_free(ModuleEntry *e) {
    free(e->name);
    free(e->module);
    free(e->args.key);
    free(e->args.key_color);
    free(e->args.format);
    free(e->args.prefix);
    free(e->args.output_color);
    free(e->args.type);
    json_free(e->raw);
}

int config_from_json_value(JfConfig *c, const JsonValue *root, char *err, size_t errn) {
    static const char *reserved[] = {"logo", "display", "general", "modules"};
    size_t i, k;
    logo_parse(&c->logo, root);
    display_parse(&c->display, root);
    general_parse(&c->general, root);
    if (root->type == JV_OBJ) {
        for (i = 0; i < root->nm; i++) {
            int skip = 0;
            for (k = 0; k < 4; k++) {
                if (!strcmp(root->members[i].key, reserved[k])) {
                    skip = 1;
                    break;
                }
            }
            if (skip)
                continue;
            if (root->members[i].val->type != JV_OBJ)
                continue;
            char *lk = strdup(root->members[i].key);
            for (char *p = lk; *p; p++) {
                if (*p >= 'A' && *p <= 'Z')
                    *p += 32;
            }
            c->options = realloc(c->options, (c->noptions + 1) * sizeof(ModuleOption));
            c->options[c->noptions].key = lk;
            c->options[c->noptions].val = root->members[i].val;
            c->noptions++;
        }
    }
    const JsonValue *mods = json_get(root, "modules");
    if (mods) {
        if (mods->type != JV_ARR) {
            snprintf(err, errn, "`modules` must be an array");
            return 0;
        }
        for (i = 0; i < mods->n; i++) {
            const JsonValue *it = mods->items[i];
            ModuleEntry e;
            memset(&e, 0, sizeof e);
            if (it->type == JV_STR) {
                e.is_object = 0;
                e.name = strdup(it->str);
            } else if (it->type == JV_OBJ) {
                const char *t = json_str(json_get(it, "type"));
                if (!t) {
                    snprintf(err, errn, "module object missing `type`");
                    entry_free(&e);
                    return 0;
                }
                e.is_object = 1;
                e.module = strdup(t);
                e.name = strdup(t);
                e.raw = json_clone(it);
                moduleargs_parse(&e.args, it);
            } else {
                snprintf(err, errn, "module entry must be a string or object");
                return 0;
            }
            c->modules = realloc(c->modules, (c->nmodules + 1) * sizeof(ModuleEntry));
            c->modules[c->nmodules++] = e;
        }
    }
    return 1;
}

int config_from_jsonc(JfConfig *c, const char *text, char *err, size_t errn) {
    JsonValue *root = json_parse(text, err, errn);
    if (!root)
        return 0;
    int ok = config_from_json_value(c, root, err, errn);
    json_free(root);
    return ok;
}

const JsonValue *config_module_options(const JfConfig *c, const char *module) {
    char lk[128];
    size_t i = 0;
    while (module[i] && i + 1 < sizeof lk) {
        char ch = module[i];
        lk[i++] = (char)(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
    }
    lk[i] = 0;
    for (i = 0; i < c->noptions; i++) {
        if (!strcmp(c->options[i].key, lk))
            return c->options[i].val;
    }
    return NULL;
}

const char *module_entry_name(const ModuleEntry *e) {
    return e->is_object ? e->module : e->name;
}

void config_free(JfConfig *c) {
    size_t i;
    free(c->logo.logo_type);
    free(c->logo.source);
    free(c->logo.color);
    for (i = 0; i < c->logo.ncolor_map; i++) {
        free(c->logo.color_map[i].line);
        free(c->logo.color_map[i].color);
    }
    free(c->logo.color_map);
    free(c->logo.logo_key);
    free(c->logo.animation);
    free(c->logo.style);
    free(c->logo.chars);
    free(c->logo.sharkvis);
    free(c->display.separator);
    free(c->display.separator_color);
    free(c->display.key_color);
    free(c->display.title_color);
    free(c->display.bar_border_left);
    free(c->display.bar_border_right);
    free(c->display.bar_char_elapsed);
    free(c->display.bar_char_total);
    free(c->display.pipe);
    free(c->general.module_overflow);
    free(c->general.file_icon);
    free(c->general.file_style);
    free(c->general.file_size);
    free(c->general.file_lines);
    free(c->general.player);
    free(c->general.custom_cpu_name);
    free(c->general.custom_temperature_name);
    for (i = 0; i < c->general.nvideo_decoders; i++)
        free(c->general.video_decoders[i]);
    free(c->general.video_decoders);
    for (i = 0; i < c->general.nvideo_encoders; i++)
        free(c->general.video_encoders[i]);
    free(c->general.video_encoders);
    free(c->general.target_path);
    for (i = 0; i < c->nmodules; i++)
        entry_free(&c->modules[i]);
    free(c->modules);
    free(c->loaded_from);
    for (i = 0; i < c->noptions; i++)
        free(c->options[i].key);
    free(c->options);
    memset(c, 0, sizeof *c);
}

static char *dup_opt(const char *s) {
    return s ? strdup(s) : NULL;
}

void config_clone(JfConfig *dst, const JfConfig *src) {
    size_t i;
    memset(dst, 0, sizeof *dst);
    dst->logo.logo_type = dup_opt(src->logo.logo_type);
    dst->logo.source = dup_opt(src->logo.source);
    dst->logo.color = dup_opt(src->logo.color);
    for (i = 0; i < src->logo.ncolor_map; i++) {
        dst->logo.color_map =
            realloc(dst->logo.color_map, (dst->logo.ncolor_map + 1) * sizeof(ColorMapEntry));
        dst->logo.color_map[dst->logo.ncolor_map].line =
            strdup(src->logo.color_map[i].line);
        dst->logo.color_map[dst->logo.ncolor_map].color =
            strdup(src->logo.color_map[i].color);
        dst->logo.ncolor_map++;
    }
    dst->logo.has_pad_top = src->logo.has_pad_top;
    dst->logo.padding_top = src->logo.padding_top;
    dst->logo.has_pad_left = src->logo.has_pad_left;
    dst->logo.padding_left = src->logo.padding_left;
    dst->logo.has_pad_right = src->logo.has_pad_right;
    dst->logo.padding_right = src->logo.padding_right;
    dst->logo.has_font_size = src->logo.has_font_size;
    dst->logo.font_size = src->logo.font_size;
    dst->logo.logo_key = dup_opt(src->logo.logo_key);
    dst->logo.has_width = src->logo.has_width;
    dst->logo.width = src->logo.width;
    dst->logo.has_height = src->logo.has_height;
    dst->logo.height = src->logo.height;
    dst->logo.animation = dup_opt(src->logo.animation);
    dst->logo.style = dup_opt(src->logo.style);
    dst->logo.chars = dup_opt(src->logo.chars);
    dst->logo.sharkvis = dup_opt(src->logo.sharkvis);
    dst->display.separator = dup_opt(src->display.separator);
    dst->display.separator_color = dup_opt(src->display.separator_color);
    dst->display.key_color = dup_opt(src->display.key_color);
    dst->display.title_color = dup_opt(src->display.title_color);
    dst->display.key_width = src->display.key_width;
    dst->display.key_width_right_aligned = src->display.key_width_right_aligned;
    dst->display.padding = src->display.padding;
    dst->display.bar_border_left = dup_opt(src->display.bar_border_left);
    dst->display.bar_border_right = dup_opt(src->display.bar_border_right);
    dst->display.bar_char_elapsed = dup_opt(src->display.bar_char_elapsed);
    dst->display.bar_char_total = dup_opt(src->display.bar_char_total);
    dst->display.bar_width = src->display.bar_width;
    dst->display.percent_type = src->display.percent_type;
    memcpy(dst->display.colors, src->display.colors, sizeof dst->display.colors);
    dst->display.pipe = dup_opt(src->display.pipe);
    dst->display.bright_color = src->display.bright_color;
    dst->display.color_align = src->display.color_align;
    dst->display.hide_cursor = src->display.hide_cursor;
    dst->display.is_smart = src->display.is_smart;
    dst->display.text_live = src->display.text_live;
    dst->general.module_overflow = dup_opt(src->general.module_overflow);
    dst->general.processes = src->general.processes;
    dst->general.thread_timeout = src->general.thread_timeout;
    dst->general.add_nested_block = src->general.add_nested_block;
    dst->general.error_output = src->general.error_output;
    dst->general.disable_linewrap = src->general.disable_linewrap;
    dst->general.show_hidden = src->general.show_hidden;
    dst->general.file_icon = dup_opt(src->general.file_icon);
    dst->general.file_style = dup_opt(src->general.file_style);
    dst->general.file_color = src->general.file_color;
    dst->general.file_size = dup_opt(src->general.file_size);
    dst->general.file_lines = dup_opt(src->general.file_lines);
    dst->general.file_max_pkg_count = src->general.file_max_pkg_count;
    dst->general.player = dup_opt(src->general.player);
    dst->general.net_dns_worker = src->general.net_dns_worker;
    dst->general.wm_worker = src->general.wm_worker;
    dst->general.custom_cpu_name = dup_opt(src->general.custom_cpu_name);
    dst->general.custom_temperature_name = dup_opt(src->general.custom_temperature_name);
    dst->general.num_fonts = src->general.num_fonts;
    dst->general.font_worker = src->general.font_worker;
    dst->general.font_prefer_mirror = src->general.font_prefer_mirror;
    dst->general.logo_title_key = src->general.logo_title_key;
    dst->general.use_a2fa = src->general.use_a2fa;
    dst->general.target_path = dup_opt(src->general.target_path);
    dst->general.lazy_packages = src->general.lazy_packages;
    for (i = 0; i < src->nmodules; i++) {
        const ModuleEntry *e = &src->modules[i];
        dst->modules = realloc(dst->modules, (dst->nmodules + 1) * sizeof(ModuleEntry));
        ModuleEntry *d = &dst->modules[dst->nmodules++];
        memset(d, 0, sizeof *d);
        d->is_object = e->is_object;
        d->name = dup_opt(e->name);
        d->module = dup_opt(e->module);
        d->raw = json_clone(e->raw);
        if (e->args.key)
            d->args.key = strdup(e->args.key);
        if (e->args.key_color)
            d->args.key_color = strdup(e->args.key_color);
        if (e->args.format)
            d->args.format = strdup(e->args.format);
        if (e->args.prefix)
            d->args.prefix = strdup(e->args.prefix);
        d->args.hide_if_empty = e->args.hide_if_empty;
        d->args.hide_if_not_supported = e->args.hide_if_not_supported;
        if (e->args.output_color)
            d->args.output_color = strdup(e->args.output_color);
        d->args.title = e->args.title;
        if (e->args.type)
            d->args.type = strdup(e->args.type);
        d->args.has_fmt = e->args.has_fmt;
    }
    dst->loaded_from = dup_opt(src->loaded_from);
    for (i = 0; i < src->noptions; i++) {
        dst->options = realloc(dst->options, (dst->noptions + 1) * sizeof(ModuleOption));
        dst->options[dst->noptions].key = strdup(src->options[i].key);
        dst->options[dst->noptions].val = json_clone(src->options[i].val);
        dst->noptions++;
    }
}
