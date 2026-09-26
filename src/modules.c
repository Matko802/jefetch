#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "config.h"
#include "detect.h"
#include "json.h"
#include "modules.h"
#include "print.h"

const ModuleInfo JF_MODULES[] = {
    {"Battery", "Print battery information", 1},
    {"BIOS", "Print first-stage bootloader information (name, version, release date, etc.)", 1},
    {"Bluetooth", "List connected Bluetooth devices", 0},
    {"BluetoothRadio", "List Bluetooth radios (supported versions, vendors, etc.)", 0},
    {"Board", "Print motherboard name and other information", 1},
    {"Bootmgr", "Print second-stage bootloader information (name, firmware, etc.)", 1},
    {"Break", "Print an empty line", 0},
    {"Brightness", "Print the current brightness level of your monitors", 1},
    {"Btrfs", "Print Linux BTRFS volumes", 1},
    {"Camera", "Print available cameras", 1},
    {"Chassis", "Print chassis type information (desktop, laptop, etc.)", 1},
    {"Codec", "Print hardware video acceleration codec types (decode / encode)", 1},
    {"Command", "Run custom shell scripts", 1},
    {"Colors", "Display the terminal's 16-color palette", 0},
    {"CPU", "Print CPU name, frequency, etc", 1},
    {"CPUCache", "Print CPU caches", 1},
    {"CPUUsage", "Print CPU usage", 1},
    {"Cursor", "Print cursor style name", 1},
    {"Custom", "Print a custom string, with or without key", 1},
    {"DateTime", "Print the current date and time", 1},
    {"DesktopEnvironment", "Print the desktop environment", 1},
    {"Disk", "Print mounted disks and their usage", 1},
    {"DiskIO", "Print disk I/O", 1},
    {"Display", "Print displays and their specifications (size, resolution and refresh rate)", 1},
    {"DNS", "Print configured DNS servers", 1},
    {"Editor", "Print information about default editor ($VISUAL or $EDITOR)", 0},
    {"Font", "Print system font names", 1},
    {"Gamepad", "List connected gamepads", 1},
    {"GPU", "Print GPU names, memory sizes and core counts", 1},
    {"Host", "Print your computer's product name", 1},
    {"InitSystem", "Print init system (pid 1) name and version", 1},
    {"Kernel", "Print system kernel version", 0},
    {"Keyboard", "List connected keyboards", 1},
    {"LM", "Print login manager (desktop manager) name and version", 1},
    {"Loadavg", "Print system load averages", 1},
    {"Locale", "Print user locale", 0},
    {"LocalIp", "List local IP addresses (IPv4 or IPv6), MAC addresses, etc", 1},
    {"Logo", "Query built-in logo for JSON output", 1},
    {"Media", "Print the name of currently playing song", 1},
    {"Memory", "Print system memory usage information", 1},
    {"Monitor", "Same as Display module, but with a different default output format", 1},
    {"Mouse", "List connected mice", 1},
    {"NetIO", "Print network I/O throughput", 1},
    {"OpenCL", "Print the highest OpenCL version supported by the GPU", 1},
    {"OpenGL", "Print the highest OpenGL version supported by the GPU", 1},
    {"OS", "Print the OS or Linux distribution name and version", 1},
    {"Packages", "List installed package managers and count of installed packages", 1},
    {"PhysicalDisk", "Print physical disk information", 1},
    {"PhysicalMemory", "Print system physical memory devices", 1},
    {"Player", "Print the music player name that is currently active", 0},
    {"PowerAdapter", "Print power adapter name and charging watts", 1},
    {"Processes", "Print number of running processes", 1},
    {"PublicIp", "Print your public IP address and related information", 1},
    {"Separator", "Print a separator line", 0},
    {"Shell", "Print the current shell name and version", 1},
    {"Sound", "Print sound devices, volume levels, etc", 1},
    {"Swap", "Print swap (paging file) space usage", 1},
    {"Terminal", "Print the current terminal name and version", 1},
    {"TerminalFont", "Print the font name and size used by the current terminal", 1},
    {"TerminalSize", "Print the current terminal size", 1},
    {"TerminalTheme", "Print the current terminal theme (foreground and background colors)", 1},
    {"Title", "Print the title, including your username and hostname", 0},
    {"Theme", "Print the current desktop environment theme", 1},
    {"TPM", "Print information about the Trusted Platform Module (TPM) security device", 1},
    {"Uptime", "Print how long the system has been running", 1},
    {"Users", "Print users who are currently logged in", 1},
    {"Version", "Print the Fastfetch version and build information", 0},
    {"Vulkan", "Print the highest Vulkan version supported by the GPU", 1},
    {"Wallpaper", "Print the file path of the current wallpaper", 1},
    {"Weather", "Print weather information", 1},
    {"WM", "Print the window manager name and version", 1},
    {"Wifi", "Print connected Wi-Fi info (SSID, connection and security protocol)", 1},
    {"WMTheme", "Print the current window manager theme", 1},
    {"Zpool", "Print ZFS storage pools", 1},
};

const size_t JF_NMODULES = sizeof(JF_MODULES) / sizeof(JF_MODULES[0]);

const ModuleInfo *module_from_name(const char *name) {
    char l[64];
    size_t i = 0;
    while (name[i] && i + 1 < sizeof l) {
        char c = name[i];
        l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    l[i] = 0;
    for (size_t k = 0; k < JF_NMODULES; k++) {
        const char *m = JF_MODULES[k].name;
        size_t j = 0;
        char ml[64];
        while (m[j] && j + 1 < sizeof ml) {
            char c = m[j];
            ml[j++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        ml[j] = 0;
        if (!strcmp(l, ml))
            return &JF_MODULES[k];
    }
    return NULL;
}

void module_output_free_contents(ModuleOutput *o) {
    for (size_t i = 0; i < o->nvalues; i++)
        free(o->values[i]);
    free(o->values);
    for (size_t i = 0; i < o->nper; i++)
        free(o->per_value_keys[i]);
    free(o->per_value_keys);
    free(o->key);
    memset(o, 0, sizeof *o);
}

static void args_copy(ModuleArgs *dst, const ModuleArgs *src) {
    memset(dst, 0, sizeof *dst);
    if (src->key)
        dst->key = strdup(src->key);
    if (src->key_color)
        dst->key_color = strdup(src->key_color);
    if (src->format)
        dst->format = strdup(src->format);
    if (src->prefix)
        dst->prefix = strdup(src->prefix);
    dst->hide_if_empty = src->hide_if_empty;
    dst->hide_if_not_supported = src->hide_if_not_supported;
    if (src->output_color)
        dst->output_color = strdup(src->output_color);
    dst->has_output_custom_color = src->has_output_custom_color;
    dst->output_custom_color = src->output_custom_color;
    dst->title = src->title;
    if (src->type)
        dst->type = strdup(src->type);
    dst->has_fmt = src->has_fmt;
}

void module_instance_init(ModuleInstance *inst, const ModuleEntry *e) {
    memset(inst, 0, sizeof *inst);
    inst->module = strdup(module_entry_name(e));
    args_copy(&inst->args, &e->args);
    inst->raw = json_clone(e->raw);
}

void module_instance_free(ModuleInstance *inst) {
    free(inst->module);
    free(inst->args.key);
    free(inst->args.key_color);
    free(inst->args.format);
    free(inst->args.prefix);
    free(inst->args.output_color);
    free(inst->args.type);
    json_free(inst->raw);
    memset(inst, 0, sizeof *inst);
}

static ModuleOutput *out_supported(const char *key, char **values, size_t n) {
    ModuleOutput *o = calloc(1, sizeof *o);
    o->key = strdup(key);
    o->values = values;
    o->nvalues = n;
    o->supported = 1;
    return o;
}

static ModuleOutput *out_single(const char *key, const char *value) {
    char **v = malloc(sizeof(char *));
    v[0] = strdup(value);
    return out_supported(key, v, 1);
}

static void pct_colored(unsigned pct, char *out, size_t n) {
    const char *color = pct <= 50 ? "green" : (pct <= 80 ? "bright_yellow" : "bright_red");
    ApplyResult ar = jf_color_code_to_ansi(color);
    char tmp[16];
    snprintf(tmp, sizeof tmp, "%u", pct);
    if (ar.is_ansi)
        snprintf(out, n, "(%s%s%%%s)", ar.start, tmp, ar.end);
    else
        snprintf(out, n, "(%s%%)", tmp);
    apply_result_free(&ar);
}

static ModuleOutput *render_custom(const ModuleInstance *inst) {
    (void)inst;
    return out_single("", "");
}

static ModuleOutput *render_command(const ModuleInstance *inst) {
    if (!inst->raw || inst->raw->type != JV_OBJ)
        return NULL;
    const char *text = json_str(json_get(inst->raw, "text"));
    if (!text)
        return NULL;
    const char *args[] = {"-c", text, NULL};
    char *out = detect_run_capture("/bin/sh", args);
    if (!out)
        return NULL;
    char *e = out + strlen(out);
    while (e > out && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n'))
        *--e = 0;
    ModuleOutput *r = NULL;
    if (*out) {
        char **v = malloc(sizeof(char *));
        v[0] = out;
        r = out_supported("", v, 1);
    } else {
        free(out);
    }
    return r;
}

typedef enum { SYM_BLOCK, SYM_BACKGROUND, SYM_CIRCLE, SYM_DIAMOND, SYM_TRIANGLE,
               SYM_SQUARE, SYM_STAR } ColorsSymbol;
typedef enum { BRI_DEFAULT, BRI_NORMAL, BRI_LIGHT } ColorsBrightness;

typedef struct {
    ColorsSymbol symbol;
    size_t pad;
    size_t width;
    unsigned lo;
    unsigned hi;
    ColorsBrightness brightness;
} ColorsOpts;

static const JsonValue *opt_val(const ModuleInstance *inst, const JfConfig *cfg,
                                const char *key) {
    const JsonValue *v = inst->raw ? json_get(inst->raw, key) : NULL;
    if (v)
        return v;
    const JsonValue *o = config_module_options(cfg, "colors");
    return o ? json_get(o, key) : NULL;
}

static void colors_rows(const ColorsOpts *opts, char ***rows, size_t *n) {
    *rows = NULL;
    *n = 0;
    JfBuf pad;
    memset(&pad, 0, sizeof pad);
    for (size_t i = 0; i < opts->pad; i++)
        jf_buf_putc(&pad, ' ');
    if (opts->symbol == SYM_BLOCK || opts->symbol == SYM_BACKGROUND) {
        if (opts->brightness != BRI_LIGHT) {
            JfBuf row;
            memset(&row, 0, sizeof row);
            unsigned end = opts->hi < 7 ? opts->hi : 7;
            for (unsigned i = opts->lo; i <= end; i++) {
                char tmp[32];
                if (opts->symbol == SYM_BLOCK) {
                    if (i > 7)
                        snprintf(tmp, sizeof tmp, "\x1b[9%um", i - 8);
                    else
                        snprintf(tmp, sizeof tmp, "\x1b[3%um", i);
                    jf_buf_put(&row, tmp);
                    for (size_t k = 0; k < opts->width; k++)
                        jf_buf_put(&row, "\xe2\x96\x88");
                } else {
                    snprintf(tmp, sizeof tmp, "\x1b[4%um", i);
                    jf_buf_put(&row, tmp);
                    for (size_t k = 0; k < opts->width; k++)
                        jf_buf_putc(&row, ' ');
                }
            }
            if (row.len > 0) {
                jf_buf_put(&row, "\x1b[m");
                *rows = realloc(*rows, (*n + 1) * sizeof(char *));
                JfBuf full;
                memset(&full, 0, sizeof full);
                jf_buf_put(&full, pad.data ? pad.data : "");
                jf_buf_put(&full, row.data);
                (*rows)[(*n)++] = full.data ? full.data : strdup("");
            }
            jf_buf_free(&row);
        }
        if (opts->brightness != BRI_NORMAL) {
            const char *term = getenv("TERM");
            JfBuf row;
            memset(&row, 0, sizeof row);
            int blink = opts->symbol == SYM_BACKGROUND && (!term || strncmp(term, "xterm", 5));
            if (blink)
                jf_buf_put(&row, "\x1b[5m");
            unsigned start = opts->lo > 8 ? opts->lo : 8;
            for (unsigned i = start; i <= opts->hi; i++) {
                char tmp[32];
                if (opts->symbol == SYM_BLOCK) {
                    snprintf(tmp, sizeof tmp, "\x1b[9%um", i - 8);
                    jf_buf_put(&row, tmp);
                    for (size_t k = 0; k < opts->width; k++)
                        jf_buf_put(&row, "\xe2\x96\x88");
                } else {
                    snprintf(tmp, sizeof tmp, "\x1b[10%um", i - 8);
                    jf_buf_put(&row, tmp);
                    for (size_t k = 0; k < opts->width; k++)
                        jf_buf_putc(&row, ' ');
                }
            }
            char *bare = strdup(row.data ? row.data : "");
            char *w = bare;
            while ((w = strstr(w, "\x1b[5m")) != NULL)
                memmove(w, w + 4, strlen(w + 4) + 1);
            if (bare[0]) {
                jf_buf_put(&row, "\x1b[m");
                *rows = realloc(*rows, (*n + 1) * sizeof(char *));
                JfBuf full;
                memset(&full, 0, sizeof full);
                jf_buf_put(&full, pad.data ? pad.data : "");
                jf_buf_put(&full, row.data);
                (*rows)[(*n)++] = full.data ? full.data : strdup("");
            }
            free(bare);
            jf_buf_free(&row);
        }
    } else {
        const char *glyph = "███ ";
        if (opts->symbol == SYM_CIRCLE)
            glyph = "● ";
        else if (opts->symbol == SYM_DIAMOND)
            glyph = "◆ ";
        else if (opts->symbol == SYM_TRIANGLE)
            glyph = "▲ ";
        else if (opts->symbol == SYM_SQUARE)
            glyph = "■ ";
        else if (opts->symbol == SYM_STAR)
            glyph = "★ ";
        JfBuf row;
        memset(&row, 0, sizeof row);
        if (opts->brightness == BRI_DEFAULT) {
            for (int i = 8; i >= 1; i--) {
                char tmp[32];
                snprintf(tmp, sizeof tmp, "\x1b[38;5;%dm%s", i, glyph);
                jf_buf_put(&row, tmp);
            }
        } else {
            char prefix = opts->brightness == BRI_NORMAL ? '3' : '9';
            for (int i = 0; i <= 7; i++) {
                char tmp[32];
                snprintf(tmp, sizeof tmp, "\x1b[%c%dm%s", prefix, i, glyph);
                jf_buf_put(&row, tmp);
            }
        }
        while (row.len > 0 && row.data[row.len - 1] == ' ')
            row.data[--row.len] = 0;
        jf_buf_put(&row, "\x1b[m");
        *rows = malloc(sizeof(char *));
        JfBuf full;
        memset(&full, 0, sizeof full);
        jf_buf_put(&full, pad.data ? pad.data : "");
        jf_buf_put(&full, row.data);
        (*rows)[(*n)++] = full.data ? full.data : strdup("");
        jf_buf_free(&row);
    }
    jf_buf_free(&pad);
}

static ModuleOutput *render_colors(const ModuleInstance *inst, const JfConfig *cfg) {
    ColorsOpts opts;
    opts.symbol = SYM_BACKGROUND;
    opts.pad = 0;
    opts.width = 3;
    opts.lo = 0;
    opts.hi = 15;
    opts.brightness = BRI_DEFAULT;
    const JsonValue *v = opt_val(inst, cfg, "symbol");
    const char *s = v ? json_str(v) : NULL;
    if (s) {
        char l[32];
        size_t i = 0;
        while (s[i] && i + 1 < sizeof l) {
            char c = s[i];
            l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        l[i] = 0;
        if (!strcmp(l, "block"))
            opts.symbol = SYM_BLOCK;
        else if (!strcmp(l, "background"))
            opts.symbol = SYM_BACKGROUND;
        else if (!strcmp(l, "circle"))
            opts.symbol = SYM_CIRCLE;
        else if (!strcmp(l, "diamond"))
            opts.symbol = SYM_DIAMOND;
        else if (!strcmp(l, "triangle"))
            opts.symbol = SYM_TRIANGLE;
        else if (!strcmp(l, "square"))
            opts.symbol = SYM_SQUARE;
        else if (!strcmp(l, "star"))
            opts.symbol = SYM_STAR;
        else
            opts.symbol = SYM_BACKGROUND;
    }
    v = opt_val(inst, cfg, "paddingLeft");
    unsigned long long n = 0;
    if (v && json_u64(v, &n))
        opts.pad = n > 64 ? 64 : (size_t)n;
    v = opt_val(inst, cfg, "block");
    if (v && v->type == JV_OBJ) {
        const JsonValue *w = json_get(v, "width");
        if (w && json_u64(w, &n)) {
            if (n < 1)
                n = 1;
            if (n > 64)
                n = 64;
            opts.width = (size_t)n;
        }
        const JsonValue *rg = json_get(v, "range");
        if (rg && rg->type == JV_ARR && rg->n == 2) {
            unsigned long long a = 0, b = 0;
            if (json_u64(rg->items[0], &a) && json_u64(rg->items[1], &b) && a <= b &&
                b <= 15) {
                opts.lo = (unsigned)a;
                opts.hi = (unsigned)b;
            }
        }
    }
    v = opt_val(inst, cfg, "brightness");
    s = v ? json_str(v) : NULL;
    if (s) {
        char l[32];
        size_t i = 0;
        while (s[i] && i + 1 < sizeof l) {
            char c = s[i];
            l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        l[i] = 0;
        if (!strcmp(l, "normal"))
            opts.brightness = BRI_NORMAL;
        else if (!strcmp(l, "light"))
            opts.brightness = BRI_LIGHT;
        else
            opts.brightness = BRI_DEFAULT;
    }
    char **rows = NULL;
    size_t nr = 0;
    colors_rows(&opts, &rows, &nr);
    if (nr == 0)
        return NULL;
    return out_supported("", rows, nr);
}

static ModuleOutput *render_datetime(const JfConfig *cfg) {
    (void)cfg;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t t = (time_t)ts.tv_sec;
    struct tm tm;
    localtime_r(&t, &tm);
    char text[64];
    snprintf(text, sizeof text, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900,
             tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return out_single("DateTime", text);
}

static ModuleOutput *render_loadavg(const JfConfig *cfg) {
    (void)cfg;
    char *text = detect_read_file("/proc/loadavg");
    if (!text)
        return NULL;
    char *save = NULL;
    char *a = strtok_r(text, " \t\n", &save);
    char *b = strtok_r(NULL, " \t\n", &save);
    char *c = strtok_r(NULL, " \t\n", &save);
    ModuleOutput *r = NULL;
    if (a && b && c) {
        char v[128];
        snprintf(v, sizeof v, "%s %s %s", a, b, c);
        r = out_single("Loadavg", v);
    }
    free(text);
    return r;
}

static ModuleOutput *render_processes(const JfConfig *cfg) {
    (void)cfg;
    DIR *dp = opendir("/proc");
    if (!dp)
        return NULL;
    size_t n = 0;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        const char *p = de->d_name;
        if (!*p)
            continue;
        int ok = 1;
        while (*p) {
            if (*p < '0' || *p > '9') {
                ok = 0;
                break;
            }
            p++;
        }
        if (ok)
            n++;
    }
    closedir(dp);
    char v[32];
    snprintf(v, sizeof v, "%zu", n);
    return out_single("Processes", v);
}

static ModuleOutput *render_locale(const JfConfig *cfg) {
    (void)cfg;
    char *l = detect_getenv("LC_ALL");
    if (!l)
        l = detect_getenv("LC_CTYPE");
    if (!l)
        l = detect_getenv("LANG");
    if (!l || !*l) {
        free(l);
        return NULL;
    }
    ModuleOutput *r = out_single("Locale", l);
    free(l);
    return r;
}

static ModuleOutput *render_swap(const JfConfig *cfg) {
    (void)cfg;
    MemoryInfo m;
    detect_memory(&m);
    if (m.swap_total == 0)
        return NULL;
    unsigned pct = 0;
    jf_percent(m.swap_used, m.swap_total, &pct);
    char ub[32], tb[32], pc[32];
    jf_format_bytes(m.swap_used, ub, sizeof ub);
    jf_format_bytes(m.swap_total, tb, sizeof tb);
    pct_colored(pct, pc, sizeof pc);
    char v[128];
    snprintf(v, sizeof v, "%s / %s %s", ub, tb, pc);
    return out_single("Swap", v);
}

static ModuleOutput *render_title(const JfConfig *cfg) {
    UserInfo u;
    detect_user(&u);
    char user[512], host[512];
    const DisplayConfig *d = &cfg->display;
    if (d->title_color) {
        ApplyResult ar = jf_color_code_to_ansi(d->title_color);
        if (ar.is_ansi) {
            snprintf(user, sizeof user, "%s%s%s%s", ar.start,
                     jf_live_text_suffix(d->text_live), u.user_name_part, ar.end);
            snprintf(host, sizeof host, "%s%s%s%s", ar.start,
                     jf_live_text_suffix(d->text_live), u.host_name_part, ar.end);
        } else {
            snprintf(user, sizeof user, "%s", u.user_name_part);
            snprintf(host, sizeof host, "%s", u.host_name_part);
        }
        apply_result_free(&ar);
    } else {
        snprintf(user, sizeof user, "%s", u.user_name_part);
        snprintf(host, sizeof host, "%s", u.host_name_part);
    }
    char v[1024];
    snprintf(v, sizeof v, "%s@%s", user, host);
    return out_single("", v);
}

static ModuleOutput *render_os(const JfConfig *cfg) {
    (void)cfg;
    OsInfo o;
    detect_os(&o);
    if (!o.name[0])
        return NULL;
    char v[512];
    if (!o.version[0])
        snprintf(v, sizeof v, "%s %s", o.name, o.arch);
    else
        snprintf(v, sizeof v, "%s %s %s", o.name, o.version, o.arch);
    return out_single("OS", v);
}

static ModuleOutput *render_kernel(const JfConfig *cfg) {
    (void)cfg;
    KernelInfo k;
    detect_kernel(&k);
    if (!k.release[0])
        return NULL;
    char v[256], *t = v;
    snprintf(v, sizeof v, "Linux %s", k.release);
    while (*t == ' ' || *t == '\t')
        t++;
    char vv[256];
    snprintf(vv, sizeof vv, "%s", t);
    return out_single("Kernel", vv);
}

static ModuleOutput *render_uptime(const JfConfig *cfg) {
    (void)cfg;
    UptimeInfo u;
    detect_uptime(&u);
    if (u.uptime_secs == 0)
        return NULL;
    char text[128];
    jf_format_uptime(u.uptime_secs, text, sizeof text);
    return out_single("Uptime", text);
}

static ModuleOutput *render_memory(const JfConfig *cfg) {
    (void)cfg;
    MemoryInfo m;
    detect_memory(&m);
    if (m.mem_total == 0)
        return NULL;
    unsigned pct = 0;
    jf_percent(m.mem_used, m.mem_total, &pct);
    char ub[32], tb[32], pc[32];
    jf_format_bytes(m.mem_used, ub, sizeof ub);
    jf_format_bytes(m.mem_total, tb, sizeof tb);
    pct_colored(pct, pc, sizeof pc);
    char v[128];
    snprintf(v, sizeof v, "%s / %s %s", ub, tb, pc);
    return out_single("Memory", v);
}

static ModuleOutput *render_shell(const JfConfig *cfg) {
    (void)cfg;
    ShellInfo s;
    detect_shell(&s);
    if (!s.shell_path[0])
        return NULL;
    char v[256];
    if (!s.shell_version[0])
        snprintf(v, sizeof v, "%s", s.shell_base_name);
    else
        snprintf(v, sizeof v, "%s %s", s.shell_base_name, s.shell_version);
    return out_single("Shell", v);
}

static ModuleOutput *render_wm(const JfConfig *cfg) {
    (void)cfg;
    WmInfo w;
    detect_wm(&w);
    if (!w.name[0])
        return NULL;
    char v[256];
    snprintf(v, sizeof v, "%s", w.name);
    if (w.version[0]) {
        strncat(v, " ", sizeof v - strlen(v) - 1);
        strncat(v, w.version, sizeof v - strlen(v) - 1);
    }
    if (w.session_type[0]) {
        strncat(v, " (", sizeof v - strlen(v) - 1);
        strncat(v, w.session_type, sizeof v - strlen(v) - 1);
        strncat(v, ")", sizeof v - strlen(v) - 1);
    }
    return out_single("WM", v);
}

static ModuleOutput *render_initsystem(const JfConfig *cfg) {
    (void)cfg;
    InitSystemInfo i;
    detect_initsystem(&i);
    if (!i.name[0])
        return NULL;
    char v[256];
    if (!i.version[0])
        snprintf(v, sizeof v, "%s", i.name);
    else
        snprintf(v, sizeof v, "%s %s", i.name, i.version);
    return out_single("Init System", v);
}

static ModuleOutput *render_lm(const JfConfig *cfg) {
    (void)cfg;
    LoginManagerInfo m;
    detect_lm(&m);
    if (!m.name[0])
        return NULL;
    char v[256];
    if (!m.version[0]) {
        snprintf(v, sizeof v, "%s", m.name);
    } else {
        char ml[128], nl[128];
        size_t k = 0;
        while (m.version[k] && k + 1 < sizeof ml) {
            char c = m.version[k];
            ml[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        ml[k] = 0;
        k = 0;
        while (m.name[k] && k + 1 < sizeof nl) {
            char c = m.name[k];
            nl[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        nl[k] = 0;
        if (strstr(ml, nl))
            snprintf(v, sizeof v, "%s", m.version);
        else
            snprintf(v, sizeof v, "%s %s", m.name, m.version);
    }
    return out_single("LM", v);
}

static ModuleOutput *render_de(const JfConfig *cfg) {
    (void)cfg;
    DeInfo d;
    detect_de(&d);
    if (!d.name[0])
        return NULL;
    WmInfo w;
    detect_wm(&w);
    if (w.name[0]) {
        char dl[128], wl[128];
        size_t k = 0;
        while (d.name[k] && k + 1 < sizeof dl) {
            char c = d.name[k];
            dl[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        dl[k] = 0;
        k = 0;
        while (w.name[k] && k + 1 < sizeof wl) {
            char c = w.name[k];
            wl[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        wl[k] = 0;
        if (!strcmp(dl, wl))
            return NULL;
    }
    return out_single("DE", d.name);
}

static ModuleOutput *render_terminal(const JfConfig *cfg) {
    (void)cfg;
    TerminalInfo t;
    detect_terminal(&t);
    if (!t.name[0])
        return NULL;
    char v[2048];
    if (t.exe[0])
        snprintf(v, sizeof v, "%s", t.exe);
    else if (!t.version[0])
        snprintf(v, sizeof v, "%s", t.name);
    else
        snprintf(v, sizeof v, "%s %s", t.name, t.version);
    return out_single("Terminal", v);
}

static ModuleOutput *render_terminal_font(const JfConfig *cfg) {
    (void)cfg;
    TerminalInfo t;
    detect_terminal(&t);
    if (!t.font[0])
        return NULL;
    return out_single("Terminal Font", t.font);
}

static int packages_owns_format_inner(const char *fmt) {
    if (strstr(fmt, "{all}"))
        return 1;
    static const char *keys[] = {"{flatpak-system}", "{flatpak-user}", "{nix-system}",
                                 "{nix-user}", "{nix-default}", "{nix}", NULL};
    for (int i = 0; keys[i]; i++) {
        if (strstr(fmt, keys[i]))
            return 1;
    }
    return 0;
}

int module_packages_owns_format(const char *fmt) {
    return packages_owns_format_inner(fmt);
}

static char *expand_packages_template(const char *fmt, const PkgAmount *amounts, size_t n) {
    JfBuf out;
    memset(&out, 0, sizeof out);
    jf_buf_put(&out, fmt);
    int touched = 0;
    char *pos = strstr(out.data ? out.data : "", "{all}");
    if (pos) {
        size_t total = 0;
        for (size_t i = 0; i < n; i++)
            total += amounts[i].count;
        char tmp[32];
        snprintf(tmp, sizeof tmp, "%zu", total);
        size_t idx = (size_t)(pos - out.data);
        JfBuf nb;
        memset(&nb, 0, sizeof nb);
        jf_buf_putn(&nb, out.data, idx);
        jf_buf_put(&nb, tmp);
        jf_buf_put(&nb, out.data + idx + 5);
        jf_buf_free(&out);
        out = nb;
        touched = 1;
    }
    for (size_t i = 0; i < n; i++) {
        char key[128];
        snprintf(key, sizeof key, "{%s}", amounts[i].name);
        char *at = out.data ? strstr(out.data, key) : NULL;
        if (at) {
            char tmp[32];
            snprintf(tmp, sizeof tmp, "%zu", amounts[i].count);
            size_t idx = (size_t)(at - out.data);
            JfBuf nb;
            memset(&nb, 0, sizeof nb);
            jf_buf_putn(&nb, out.data, idx);
            jf_buf_put(&nb, tmp);
            jf_buf_put(&nb, out.data + idx + strlen(key));
            jf_buf_free(&out);
            out = nb;
            touched = 1;
        }
    }
    if (!touched) {
        jf_buf_free(&out);
        return NULL;
    }
    return out.data ? out.data : strdup("");
}

static ModuleOutput *render_packages(const ModuleInstance *inst, const JfConfig *cfg) {
    PackagesInfo p;
    detect_packages(&p);
    if (p.n == 0) {
        detect_packages_free(&p);
        return NULL;
    }
    const char *fmt = inst->args.format;
    if (!fmt) {
        const JsonValue *o = config_module_options(cfg, "packages");
        const JsonValue *f = o ? json_get(o, "format") : NULL;
        fmt = f ? json_str(f) : NULL;
    }
    ModuleOutput *r = NULL;
    if (fmt) {
        char *expanded = expand_packages_template(fmt, p.amounts, p.n);
        if (expanded) {
            r = out_single("Packages", expanded);
            free(expanded);
            detect_packages_free(&p);
            return r;
        }
    }
    int combined = 0;
    {
        const JsonValue *v = inst->raw ? json_get(inst->raw, "combined") : NULL;
        int b = 0;
        if (v && json_bool(v, &b) && b)
            combined = 1;
    }
    if (!combined) {
        const JsonValue *o = config_module_options(cfg, "packages");
        const JsonValue *v = o ? json_get(o, "combined") : NULL;
        int b = 0;
        if (v && json_bool(v, &b) && b)
            combined = 1;
    }
    if (combined) {
        size_t total = 0;
        for (size_t i = 0; i < p.n; i++)
            total += p.amounts[i].count;
        char v[32];
        snprintf(v, sizeof v, "%zu", total);
        r = out_single("Packages", v);
    } else {
        JfBuf sb;
        memset(&sb, 0, sizeof sb);
        for (size_t i = 0; i < p.n; i++) {
            if (i > 0)
                jf_buf_put(&sb, ", ");
            char tmp[160];
            char low[128];
            size_t k = 0;
            while (p.amounts[i].name[k] && k + 1 < sizeof low) {
                char c = p.amounts[i].name[k];
                low[k++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
            }
            low[k] = 0;
            snprintf(tmp, sizeof tmp, "%zu (%s)", p.amounts[i].count, low);
            jf_buf_put(&sb, tmp);
        }
        r = out_single("Packages", sb.data ? sb.data : "");
        jf_buf_free(&sb);
    }
    detect_packages_free(&p);
    return r;
}

static ModuleOutput *render_board(const JfConfig *cfg) {
    (void)cfg;
    BoardInfo b;
    detect_board(&b);
    if (!b.name[0])
        return NULL;
    char v[512];
    if (!b.version[0])
        snprintf(v, sizeof v, "%s", b.name);
    else
        snprintf(v, sizeof v, "%s (%s)", b.name, b.version);
    return out_single("Board", v);
}

static ModuleOutput *render_host(const JfConfig *cfg) {
    (void)cfg;
    char name[256];
    detect_board_product_name(name, sizeof name);
    char l[256];
    size_t i = 0;
    while (name[i] && i + 1 < sizeof l) {
        char c = name[i];
        l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    l[i] = 0;
    int generic = !strcmp(l, "system product name") || !strcmp(l, "to be filled by o.e.m.") ||
                  !strcmp(l, "default string") || !strcmp(l, "system name") ||
                  !strcmp(l, "invalid") || strstr(l, "to be filled") || strstr(l, "default string");
    if (!name[0] || generic)
        return NULL;
    return out_single("Host", name);
}

static ModuleOutput *render_cpu(const ModuleInstance *inst) {
    CpuInfo c;
    detect_cpu(&c);
    if (!c.model[0])
        return NULL;
    char model[256];
    snprintf(model, sizeof model, "%s", c.model);
    const char *suffix = " with Radeon Graphics";
    size_t ml = strlen(model), sl = strlen(suffix);
    if (ml >= sl && !strcmp(model + ml - sl, suffix))
        model[ml - sl] = 0;
    size_t cores = c.logical_cores ? c.logical_cores : c.physical_cores;
    char v[1024];
    int show_pe = 0;
    if (inst->raw) {
        const JsonValue *sp = json_get(inst->raw, "showPeCoreCount");
        int b = 0;
        if (sp && json_bool(sp, &b) && b)
            show_pe = 1;
    }
    if (show_pe && c.has_pe_cores && c.has_ee_cores) {
        if (c.ee_cores > 0)
            snprintf(v, sizeof v, "%s (%zuP + %zuE)", model, c.pe_cores, c.ee_cores);
        else
            snprintf(v, sizeof v, "%s (%zuP)", model, c.pe_cores);
    } else {
        snprintf(v, sizeof v, "%s (%zu)", model, cores);
    }
    unsigned long long freq = c.freq_max_mhz ? c.freq_max_mhz : c.freq_cur_mhz;
    if (freq > 0) {
        char tmp[1024];
        snprintf(tmp, sizeof tmp, "%.900s @ %.2f GHz", v, (double)freq / 1000.0);
        snprintf(v, sizeof v, "%s", tmp);
    }
    return out_single("CPU", v);
}

static ModuleOutput *render_gpu(const JfConfig *cfg) {
    (void)cfg;
    size_t n = 0;
    GpuInfo *gpus = detect_gpu(&n);
    if (n == 0) {
        detect_gpu_free(gpus, n);
        return NULL;
    }
    char **values = malloc(n * sizeof(char *));
    for (size_t i = 0; i < n; i++) {
        char tmp[512];
        if (!gpus[i].dtype[0])
            snprintf(tmp, sizeof tmp, "%s", gpus[i].model);
        else
            snprintf(tmp, sizeof tmp, "%s [%s]", gpus[i].model, gpus[i].dtype);
        values[i] = strdup(tmp);
    }
    detect_gpu_free(gpus, n);
    return out_supported("GPU", values, n);
}

static ModuleOutput *render_display(const JfConfig *cfg) {
    (void)cfg;
    DisplayInfo *ds = NULL;
    size_t n = 0;
    detect_display(&ds, &n);
    if (n == 0) {
        detect_display_free(ds, n);
        return NULL;
    }
    char **values = malloc(n * sizeof(char *));
    char **keys = malloc(n * sizeof(char *));
    for (size_t i = 0; i < n; i++) {
        double scale = (ds[i].scale > 0.1 && ds[i].scale < 10.0) ? ds[i].scale : 1.0;
        char sscale[32];
        detect_format_scale(scale, sscale, sizeof sscale);
        char v[256];
        snprintf(v, sizeof v, "%ux%u @ %sx", ds[i].width, ds[i].height, sscale);
        if (ds[i].size_in > 0) {
            char tmp[256];
            snprintf(tmp, sizeof tmp, "%.200s in %u\"", v, ds[i].size_in);
            snprintf(v, sizeof v, "%s", tmp);
        }
        if (ds[i].refresh_rate > 0) {
            char tmp[256];
            snprintf(tmp, sizeof tmp, "%.200s, %u Hz", v, ds[i].refresh_rate);
            snprintf(v, sizeof v, "%s", tmp);
        }
        if (ds[i].dtype[0]) {
            char tmp[256];
            snprintf(tmp, sizeof tmp, "%.200s [%s]", v, ds[i].dtype);
            snprintf(v, sizeof v, "%s", tmp);
        }
        values[i] = strdup(v);
        const char *dn = ds[i].model[0] ? ds[i].model : ds[i].name;
        char k[384];
        snprintf(k, sizeof k, "Display (%s)", dn);
        keys[i] = strdup(k);
    }
    detect_display_free(ds, n);
    ModuleOutput *o = out_supported("Display", values, n);
    o->per_value_keys = keys;
    o->nper = n;
    return o;
}

static ModuleOutput *render_disk(const ModuleInstance *inst, const JfConfig *cfg) {
    (void)cfg;
    const char **folders = NULL;
    size_t nfolders = 0;
    if (inst->raw && inst->raw->type == JV_OBJ) {
        for (size_t i = 0; i < inst->raw->nm; i++) {
            if (!strcmp(inst->raw->members[i].key, "folders")) {
                const char *s = json_str(inst->raw->members[i].val);
                if (s) {
                    folders = malloc(sizeof(char *));
                    folders[0] = s;
                    nfolders = 1;
                }
            }
        }
    }
    size_t n = 0;
    DiskInfo *disks = detect_disk(folders, nfolders, &n);
    free(folders);
    if (n == 0) {
        detect_disk_free(disks, n);
        return NULL;
    }
    char **values = malloc(n * sizeof(char *));
    for (size_t i = 0; i < n; i++) {
        char ub[32], tb[32], pc[32];
        unsigned pct = 0;
        jf_percent(disks[i].used, disks[i].total, &pct);
        jf_format_bytes(disks[i].used, ub, sizeof ub);
        jf_format_bytes(disks[i].total, tb, sizeof tb);
        pct_colored(pct, pc, sizeof pc);
        char v[512];
        snprintf(v, sizeof v, "%s / %s %s", ub, tb, pc);
        if (disks[i].filesystem[0]) {
            strncat(v, " - ", sizeof v - strlen(v) - 1);
            strncat(v, disks[i].filesystem, sizeof v - strlen(v) - 1);
        }
        values[i] = strdup(v);
    }
    detect_disk_free(disks, n);
    ModuleOutput *o = out_supported("Disk", values, n);
    o->repeat_key = 1;
    return o;
}

static ModuleOutput *render_battery(const JfConfig *cfg) {
    (void)cfg;
    size_t n = 0;
    BatteryInfo *bats = detect_battery(&n);
    if (n == 0) {
        detect_battery_free(bats, n);
        return NULL;
    }
    char **values = malloc(n * sizeof(char *));
    for (size_t i = 0; i < n; i++) {
        JfBuf sb;
        memset(&sb, 0, sizeof sb);
        int first = 1;
        if (bats[i].model[0]) {
            jf_buf_put(&sb, bats[i].model);
            first = 0;
        }
        if (bats[i].energy_full > 0.0 && bats[i].energy_now > 0.0) {
            char tmp[64];
            snprintf(tmp, sizeof tmp, "%.2f Wh / %.2f Wh", bats[i].energy_now,
                     bats[i].energy_full);
            if (!first)
                jf_buf_put(&sb, ", ");
            jf_buf_put(&sb, tmp);
            first = 0;
        }
        {
            char tmp[16];
            snprintf(tmp, sizeof tmp, "%u%%", bats[i].capacity_percent);
            if (!first)
                jf_buf_put(&sb, ", ");
            jf_buf_put(&sb, tmp);
            first = 0;
        }
        if (bats[i].status[0]) {
            if (!first)
                jf_buf_put(&sb, ", ");
            jf_buf_put(&sb, bats[i].status);
        }
        values[i] = sb.data ? sb.data : strdup("");
    }
    detect_battery_free(bats, n);
    return out_supported("Battery", values, n);
}

static ModuleOutput *render_users(const JfConfig *cfg) {
    (void)cfg;
    size_t n = 0;
    LoggedUser *users = detect_users(&n);
    if (n == 0) {
        detect_users_free(users, n);
        return NULL;
    }
    char **values = malloc(n * sizeof(char *));
    for (size_t i = 0; i < n; i++) {
        JfBuf sb;
        memset(&sb, 0, sizeof sb);
        jf_buf_put(&sb, users[i].user);
        if (users[i].tty[0]) {
            jf_buf_put(&sb, " (");
            jf_buf_put(&sb, users[i].tty);
            jf_buf_putc(&sb, ')');
        }
        if (users[i].host[0]) {
            jf_buf_put(&sb, " | ");
            jf_buf_put(&sb, users[i].host);
        }
        values[i] = sb.data ? sb.data : strdup("");
    }
    detect_users_free(users, n);
    return out_supported("Users", values, n);
}

static ModuleOutput *render_brightness(const JfConfig *cfg) {
    (void)cfg;
    size_t n = 0;
    BrightnessInfo *bs = detect_brightness(&n);
    if (n == 0) {
        detect_brightness_free(bs, n);
        return NULL;
    }
    char bar[64];
    jf_percent_bar(bs[0].value, bs[0].max, bar, sizeof bar);
    char v[128];
    snprintf(v, sizeof v, "%u%% %s", bs[0].percentage, bar);
    detect_brightness_free(bs, n);
    return out_single("Brightness", v);
}

static ModuleOutput *render_dns(const JfConfig *cfg) {
    (void)cfg;
    DnsInfo d;
    if (!detect_dns(&d))
        return NULL;
    char **values = malloc(d.nservers * sizeof(char *));
    for (size_t i = 0; i < d.nservers; i++)
        values[i] = strdup(d.servers[i]);
    size_t n = d.nservers;
    detect_dns_free(&d);
    return out_supported("DNS", values, n);
}

static ModuleOutput *render_localip(const JfConfig *cfg) {
    (void)cfg;
    size_t n = 0;
    IpInfo *ips = detect_localip(&n);
    if (n == 0) {
        detect_localip_free(ips, n);
        return NULL;
    }
    size_t chosen = n;
    char src[64] = "";
    if (detect_outbound_src_ip(src, sizeof src)) {
        for (size_t i = 0; i < n; i++) {
            for (size_t k = 0; k < ips[i].n4; k++) {
                if (!strcmp(ips[i].ipv4[k], src)) {
                    chosen = i;
                    break;
                }
            }
            if (chosen != n)
                break;
        }
    }
    if (chosen != n) {
        char key[128], value[128];
        snprintf(key, sizeof key, "Local IP (%s)", ips[chosen].name);
        value[0] = 0;
        if (ips[chosen].n4 > 0)
            snprintf(value, sizeof value, "%s/%u", ips[chosen].ipv4[0],
                     ips[chosen].prefix4[0]);
        else if (ips[chosen].n6 > 0)
            snprintf(value, sizeof value, "%s/%u", ips[chosen].ipv6[0],
                     ips[chosen].prefix6[0]);
        ModuleOutput *r = NULL;
        if (value[0]) {
            char **vv = malloc(sizeof(char *));
            vv[0] = strdup(value);
            r = out_supported(key, vv, 1);
            free(r->key);
            r->key = strdup(key);
        }
        detect_localip_free(ips, n);
        return r;
    }
    {
        size_t *cands = malloc(n * sizeof(size_t));
        size_t nc = 0;
        for (size_t i = 0; i < n; i++)
            cands[nc++] = i;
        if (nc > 1) {
            char def[64] = "";
            if (detect_default_iface(def, sizeof def)) {
                for (size_t i = 0; i < nc; i++) {
                    if (!strcmp(ips[cands[i]].name, def)) {
                        size_t t = cands[0];
                        cands[0] = cands[i];
                        cands[i] = t;
                        nc = 1;
                        break;
                    }
                }
            }
        }
        if (nc > 1) {
            size_t w = 0;
            for (size_t i = 0; i < nc; i++) {
                if (!detect_is_virtual(ips[cands[i]].name))
                    cands[w++] = cands[i];
            }
            if (w > 0)
                nc = w;
        }
        chosen = cands[0];
        free(cands);
    }
    char key[128], value[128];
    snprintf(key, sizeof key, "Local IP (%s)", ips[chosen].name);
    value[0] = 0;
    if (ips[chosen].n4 > 0)
        snprintf(value, sizeof value, "%s/%u", ips[chosen].ipv4[0], ips[chosen].prefix4[0]);
    else if (ips[chosen].n6 > 0)
        snprintf(value, sizeof value, "%s/%u", ips[chosen].ipv6[0], ips[chosen].prefix6[0]);
    ModuleOutput *r = NULL;
    if (value[0]) {
        char **vv = malloc(sizeof(char *));
        vv[0] = strdup(value);
        r = out_supported(key, vv, 1);
        free(r->key);
        r->key = strdup(key);
    }
    detect_localip_free(ips, n);
    return r;
}

static ModuleOutput *render_wifi(const JfConfig *cfg) {
    (void)cfg;
    size_t n = 0;
    WifiInfo *ws = detect_wifi(&n);
    if (n == 0) {
        detect_wifi_free(ws, n);
        return NULL;
    }
    char **values = malloc(n * sizeof(char *));
    for (size_t i = 0; i < n; i++) {
        char v[384];
        snprintf(v, sizeof v, "%s %s", ws[i].protocol, ws[i].name);
        if (ws[i].ssid[0]) {
            strncat(v, " (", sizeof v - strlen(v) - 1);
            strncat(v, ws[i].ssid, sizeof v - strlen(v) - 1);
            strncat(v, ")", sizeof v - strlen(v) - 1);
        }
        if (ws[i].signal_quality > 0) {
            char tmp[32];
            snprintf(tmp, sizeof tmp, " - %u%%", ws[i].signal_quality);
            strncat(v, tmp, sizeof v - strlen(v) - 1);
        }
        values[i] = strdup(v);
    }
    detect_wifi_free(ws, n);
    return out_supported("WiFi", values, n);
}

static ModuleOutput *render_publicip(const ModuleInstance *inst, const JfConfig *cfg) {
    (void)cfg;
    unsigned timeout = 1000;
    if (inst->raw && inst->raw->type == JV_OBJ) {
        for (size_t i = 0; i < inst->raw->nm; i++) {
            if (!strcmp(inst->raw->members[i].key, "timeout")) {
                unsigned long long t = 0;
                if (json_u64(inst->raw->members[i].val, &t) && t >= 100)
                    timeout = (unsigned)t;
            }
        }
    }
    char ip[64];
    if (!detect_publicip(ip, sizeof ip, timeout))
        return NULL;
    return out_single("Public IP", ip);
}

static void pretty_pango_font(const char *raw, char *out, size_t n);

static ModuleOutput *render_theme(const JfConfig *cfg, const char *which) {
    (void)cfg;
    GtkThemeInfo t;
    detect_theme(&t);
    const char *value = "";
    const char *key = "Theme";
    if (!strcmp(which, "theme")) {
        value = t.gtk_theme;
        key = "Theme";
    } else if (!strcmp(which, "icons")) {
        value = t.icon_theme;
        key = "Icons";
    } else if (!strcmp(which, "cursor")) {
        value = t.cursor_theme;
        key = "Cursor";
    } else if (!strcmp(which, "font")) {
        key = "Font";
    }
    if (!strcmp(which, "font")) {
        if (!t.font[0])
            return NULL;
        char pretty[256];
        pretty_pango_font(t.font, pretty, sizeof pretty);
        return out_single(key, pretty);
    }
    if (!value[0])
        return NULL;
    return out_single(key, value);
}

static ModuleOutput *render_dispatch(const char *name, const ModuleInstance *inst,
                                     const JfConfig *cfg) {
    if (!strcmp(name, "title"))
        return render_title(cfg);
    if (!strcmp(name, "os"))
        return render_os(cfg);
    if (!strcmp(name, "kernel"))
        return render_kernel(cfg);
    if (!strcmp(name, "uptime"))
        return render_uptime(cfg);
    if (!strcmp(name, "memory"))
        return render_memory(cfg);
    if (!strcmp(name, "shell"))
        return render_shell(cfg);
    if (!strcmp(name, "custom"))
        return render_custom(inst);
    if (!strcmp(name, "command"))
        return render_command(inst);
    if (!strcmp(name, "colors"))
        return render_colors(inst, cfg);
    if (!strcmp(name, "datetime"))
        return render_datetime(cfg);
    if (!strcmp(name, "loadavg"))
        return render_loadavg(cfg);
    if (!strcmp(name, "processes"))
        return render_processes(cfg);
    if (!strcmp(name, "locale"))
        return render_locale(cfg);
    if (!strcmp(name, "swap"))
        return render_swap(cfg);
    if (!strcmp(name, "wm"))
        return render_wm(cfg);
    if (!strcmp(name, "de"))
        return render_de(cfg);
    if (!strcmp(name, "initsystem"))
        return render_initsystem(cfg);
    if (!strcmp(name, "lm"))
        return render_lm(cfg);
    if (!strcmp(name, "terminal"))
        return render_terminal(cfg);
    if (!strcmp(name, "terminalfont"))
        return render_terminal_font(cfg);
    if (!strcmp(name, "packages"))
        return render_packages(inst, cfg);
    if (!strcmp(name, "board"))
        return render_board(cfg);
    if (!strcmp(name, "host"))
        return render_host(cfg);
    if (!strcmp(name, "cpu"))
        return render_cpu(inst);
    if (!strcmp(name, "gpu"))
        return render_gpu(cfg);
    if (!strcmp(name, "display"))
        return render_display(cfg);
    if (!strcmp(name, "disk"))
        return render_disk(inst, cfg);
    if (!strcmp(name, "battery"))
        return render_battery(cfg);
    if (!strcmp(name, "users"))
        return render_users(cfg);
    if (!strcmp(name, "brightness"))
        return render_brightness(cfg);
    if (!strcmp(name, "dns"))
        return render_dns(cfg);
    if (!strcmp(name, "localip"))
        return render_localip(cfg);
    if (!strcmp(name, "wifi"))
        return render_wifi(cfg);
    if (!strcmp(name, "publicip"))
        return render_publicip(inst, cfg);
    if (!strcmp(name, "theme"))
        return render_theme(cfg, "theme");
    if (!strcmp(name, "icons"))
        return render_theme(cfg, "icons");
    if (!strcmp(name, "cursor"))
        return render_theme(cfg, "cursor");
    if (!strcmp(name, "font"))
        return render_theme(cfg, "font");
    if (!strcmp(name, "break")) {
        ModuleOutput *o = calloc(1, sizeof *o);
        o->key = strdup("");
        o->values = malloc(sizeof(char *));
        o->values[0] = strdup("");
        o->nvalues = 1;
        o->supported = 1;
        o->blank = 1;
        return o;
    }
    return NULL;
}

static void pretty_pango_font(const char *raw, char *out, size_t n) {
    while (*raw == ' ' || *raw == '\t')
        raw++;
    const char *end = raw + strlen(raw);
    while (end > raw && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    const char *sp = end;
    while (sp > raw && *(sp - 1) != ' ')
        sp--;
    if (sp > raw && sp < end) {
        char num[64];
        size_t l = (size_t)(end - sp);
        if (l >= sizeof num)
            l = sizeof num - 1;
        memcpy(num, sp, l);
        num[l] = 0;
        char *e;
        double v = strtod(num, &e);
        if (e != num && !*e && v > 0.0) {
            size_t nl = (size_t)(sp - 1 - raw);
            char name[256];
            if (nl >= sizeof name)
                nl = sizeof name - 1;
            memcpy(name, raw, nl);
            name[nl] = 0;
            char sz[64];
            if (v == (double)(unsigned long long)v)
                snprintf(sz, sizeof sz, "%llu", (unsigned long long)v);
            else
                snprintf(sz, sizeof sz, "%s", num);
            snprintf(out, n, "%.200s (%.50spt)", name, sz);
            return;
        }
    }
    size_t l = (size_t)(end - raw);
    if (l >= n)
        l = n - 1;
    memcpy(out, raw, l);
    out[l] = 0;
}

typedef struct {
    const char *base_key;
    const ModuleInstance *inst;
    int text_live;
} KeyCtx;

static char *key_get_ph(void *ctx, const char *name) {
    KeyCtx *k = ctx;
    if (!strcmp(name, "key"))
        return strdup(k->base_key);
    return NULL;
}

static const char *key_key(void *ctx) {
    return ((KeyCtx *)ctx)->base_key;
}

static char *key_color(void *ctx, const char *name) {
    KeyCtx *k = ctx;
    if (strcmp(name, "keys"))
        return NULL;
    const char *c = k->inst->args.key_color ? k->inst->args.key_color : "";
    char *sgr = jf_named_color_sgr(c);
    if (!sgr)
        return NULL;
    size_t need = strlen(sgr) + strlen(jf_live_text_suffix(k->text_live)) + 1;
    char *o = malloc(need);
    snprintf(o, need, "%s%s", sgr, jf_live_text_suffix(k->text_live));
    free(sgr);
    return o;
}

static char *render_key_str(const char *raw_key, const char *base_key,
                            const ModuleInstance *inst, int text_live) {
    KeyCtx k = {base_key, inst, text_live};
    JfResolver r = {key_get_ph, key_key, key_color, NULL, &k};
    JfFormatResult fr = jf_format(raw_key, &r);
    return fr.text;
}

typedef struct {
    const char *key_name;
    JfPair *pairs;
    size_t n;
    const ModuleInstance *inst;
    int text_live;
} ValCtx;

static char *val_get_ph(void *ctx, const char *name) {
    ValCtx *v = ctx;
    for (size_t i = 0; i < v->n; i++) {
        const char *k = v->pairs[i].key;
        size_t a = 0, b = 0;
        while (k[a] && name[a]) {
            char ca = k[a], cb = name[a];
            if (ca >= 'A' && ca <= 'Z')
                ca += 32;
            if (cb >= 'A' && cb <= 'Z')
                cb += 32;
            if (ca != cb)
                break;
            a++;
            b++;
        }
        if (k[a] == 0 && name[b] == 0)
            return strdup(v->pairs[i].value);
    }
    char low[64];
    size_t i = 0;
    while (name[i] && i + 1 < sizeof low) {
        char c = name[i];
        low[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    low[i] = 0;
    if (!strcmp(low, "default"))
        return v->n > 0 ? strdup(v->pairs[0].value) : NULL;
    if (!strcmp(low, "all") || !strcmp(low, "1") || !strcmp(low, "value") ||
        !strcmp(low, "value1"))
        return v->n > 0 ? strdup(v->pairs[0].value) : NULL;
    return NULL;
}

static const char *val_key(void *ctx) {
    return ((ValCtx *)ctx)->key_name;
}

static char *val_color(void *ctx, const char *name) {
    ValCtx *v = ctx;
    if (strcmp(name, "keys"))
        return NULL;
    const char *c = v->inst->args.key_color ? v->inst->args.key_color : "";
    char *sgr = jf_named_color_sgr(c);
    if (!sgr)
        return NULL;
    size_t need = strlen(sgr) + strlen(jf_live_text_suffix(v->text_live)) + 1;
    char *o = malloc(need);
    snprintf(o, need, "%s%s", sgr, jf_live_text_suffix(v->text_live));
    free(sgr);
    return o;
}

static char *render_value_str(const char *fmt_str, const ModuleInstance *inst,
                              const char *value, int text_live) {
    const char *key_s = inst->args.key ? inst->args.key : "";
    JfPair pairs[2] = {{"value", value}, {"title", key_s}};
    ValCtx vc = {key_s, pairs, 2, inst, text_live};
    JfResolver r = {val_get_ph, val_key, val_color, NULL, &vc};
    JfFormatResult fr = jf_format(fmt_str, &r);
    return fr.text;
}

static ModuleOutput *render_separator_cfg(const JfConfig *cfg) {
    (void)cfg;
    UserInfo u;
    detect_user(&u);
    size_t title_len = 1 + jf_visible_len(u.user_name_part) + jf_visible_len(u.host_name_part);
    JfBuf line;
    memset(&line, 0, sizeof line);
    while (jf_visible_len(line.data ? line.data : "") < title_len)
        jf_buf_putc(&line, '-');
    ModuleOutput *o = out_supported("", NULL, 0);
    o->values = malloc(sizeof(char *));
    o->values[0] = line.data ? line.data : strdup("");
    o->nvalues = 1;
    return o;
}

ModuleOutput *module_run_instance(const ModuleInstance *inst, const JfConfig *cfg) {
    char lower[64];
    size_t i = 0;
    while (inst->module[i] && i + 1 < sizeof lower) {
        char c = inst->module[i];
        lower[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    lower[i] = 0;
    if (!strcmp(lower, "break")) {
        ModuleOutput *o = calloc(1, sizeof *o);
        o->key = strdup("");
        o->values = malloc(sizeof(char *));
        o->values[0] = strdup("");
        o->nvalues = 1;
        o->supported = 1;
        o->blank = 1;
        return o;
    }
    if (!strcmp(lower, "separator"))
        return render_separator_cfg(cfg);
    ModuleOutput *base = render_dispatch(lower, inst, cfg);
    if (!base)
        return NULL;
    char **values = base->values;
    size_t nvalues = base->nvalues;
    base->values = NULL;
    base->nvalues = 0;
    if (inst->args.format &&
        !(strcmp(lower, "packages") == 0 && module_packages_owns_format(inst->args.format))) {
        char **nv = malloc((nvalues ? nvalues : 1) * sizeof(char *));
        for (size_t k = 0; k < nvalues; k++) {
            nv[k] = render_value_str(inst->args.format, inst, values[k],
                                     cfg->display.text_live);
            free(values[k]);
        }
        free(values);
        values = nv;
    } else {
        if (nvalues == 0) {
            for (size_t k = 0; k < nvalues; k++)
                free(values[k]);
            free(values);
            values = NULL;
        }
    }
    int has_custom_key = inst->args.key != NULL;
    if (!has_custom_key && base->nper > 0 && base->nper == nvalues) {
        char **colored = malloc(nvalues * sizeof(char *));
        for (size_t k = 0; k < nvalues; k++) {
            const char *kk = base->per_value_keys[k];
            if (strchr(kk, 0x1b)) {
                colored[k] = strdup(kk);
                continue;
            }
            const char *c = inst->args.key_color ? inst->args.key_color : NULL;
            if (!c)
                c = cfg->display.key_color;
            if (c) {
                ApplyResult ar = jf_color_code_to_ansi(c);
                if (ar.is_ansi) {
                    char tmp[1024];
                    snprintf(tmp, sizeof tmp, "%s%s%s%s", ar.start,
                             jf_live_text_suffix(cfg->display.text_live), kk, ar.end);
                    colored[k] = strdup(tmp);
                    apply_result_free(&ar);
                    continue;
                }
                apply_result_free(&ar);
            }
            colored[k] = strdup(kk);
        }
        ModuleOutput *o = out_supported("", values, nvalues);
        free(o->key);
        o->key = strdup("");
        o->repeat_key = 1;
        o->per_value_keys = colored;
        o->nper = nvalues;
        module_output_free_contents(base);
        free(base);
        return o;
    }
    const char *raw_key = inst->args.key ? inst->args.key : base->key;
    char *key_rendered = render_key_str(raw_key, base->key, inst, cfg->display.text_live);
    ModuleOutput *o = out_supported("", values, nvalues);
    free(o->key);
    o->key = key_rendered;
    o->repeat_key = base->repeat_key;
    if (!strchr(o->key, 0x1b)) {
        const char *c = inst->args.key_color ? inst->args.key_color : NULL;
        if (!c)
            c = cfg->display.key_color;
        if (c) {
            ApplyResult ar = jf_color_code_to_ansi(c);
            if (ar.is_ansi) {
                char tmp[1024];
                snprintf(tmp, sizeof tmp, "%s%s%s%s", ar.start,
                         jf_live_text_suffix(cfg->display.text_live), o->key, ar.end);
                free(o->key);
                o->key = strdup(tmp);
            }
            apply_result_free(&ar);
        }
    }
    module_output_free_contents(base);
    free(base);
    return o;
}

const char *module_json_type_name(const char *name) {
    char l[64];
    size_t i = 0;
    while (name[i] && i + 1 < sizeof l) {
        char c = name[i];
        l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    l[i] = 0;
    if (!strcmp(l, "os"))
        return "OS";
    if (!strcmp(l, "kernel"))
        return "Kernel";
    if (!strcmp(l, "wm"))
        return "WM";
    if (!strcmp(l, "de"))
        return "DE";
    if (!strcmp(l, "initsystem"))
        return "InitSystem";
    if (!strcmp(l, "lm"))
        return "LM";
    if (!strcmp(l, "terminal"))
        return "Terminal";
    if (!strcmp(l, "terminalfont"))
        return "TerminalFont";
    if (!strcmp(l, "shell"))
        return "Shell";
    if (!strcmp(l, "packages"))
        return "Packages";
    if (!strcmp(l, "wmtheme"))
        return "WMTheme";
    if (!strcmp(l, "colors"))
        return "Colors";
    if (!strcmp(l, "custom"))
        return "Custom";
    if (!strcmp(l, "break"))
        return "Break";
    if (!strcmp(l, "separator"))
        return "Separator";
    if (!strcmp(l, "command"))
        return "Command";
    if (!strcmp(l, "board"))
        return "Board";
    if (!strcmp(l, "host"))
        return "Host";
    if (!strcmp(l, "cpu"))
        return "CPU";
    if (!strcmp(l, "gpu"))
        return "GPU";
    if (!strcmp(l, "display"))
        return "Display";
    if (!strcmp(l, "disk"))
        return "Disk";
    if (!strcmp(l, "memory"))
        return "Memory";
    if (!strcmp(l, "swap"))
        return "Swap";
    if (!strcmp(l, "uptime"))
        return "Uptime";
    if (!strcmp(l, "title"))
        return "Title";
    if (!strcmp(l, "battery"))
        return "Battery";
    if (!strcmp(l, "users"))
        return "Users";
    if (!strcmp(l, "brightness"))
        return "Brightness";
    if (!strcmp(l, "dns"))
        return "DNS";
    if (!strcmp(l, "localip"))
        return "LocalIp";
    if (!strcmp(l, "wifi"))
        return "Wifi";
    if (!strcmp(l, "publicip"))
        return "PublicIp";
    if (!strcmp(l, "theme"))
        return "Theme";
    if (!strcmp(l, "icons"))
        return "Icons";
    if (!strcmp(l, "cursor"))
        return "Cursor";
    if (!strcmp(l, "font"))
        return "Font";
    return "Unknown";
}

char *module_json_error(const char *name, const ModuleInstance *inst,
                        const JfConfig *cfg) {
    (void)inst;
    (void)cfg;
    char l[64];
    size_t i = 0;
    while (name[i] && i + 1 < sizeof l) {
        char c = name[i];
        l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    l[i] = 0;
    if (!strcmp(l, "custom") || !strcmp(l, "colors") || !strcmp(l, "break") ||
        !strcmp(l, "separator"))
        return strdup("Unsupported for JSON format");
    if (!strcmp(l, "wmtheme")) {
        WmInfo w;
        detect_wm(&w);
        char *o = malloc(strlen(w.name) + 16);
        snprintf(o, strlen(w.name) + 16, "Unknown WM: %s", w.name);
        return o;
    }
    if (!strcmp(l, "terminalfont")) {
        TerminalInfo t;
        detect_terminal(&t);
        char *o = malloc(strlen(t.name) + 22);
        snprintf(o, strlen(t.name) + 22, "Unknown terminal: %s", t.name);
        return o;
    }
    return NULL;
}

static JsonValue *jstr(const char *s) {
    JsonValue *j = calloc(1, sizeof *j);
    j->type = JV_STR;
    j->str = strdup(s ? s : "");
    return j;
}

static JsonValue *juint(unsigned long long v) {
    JsonValue *j = calloc(1, sizeof *j);
    j->type = JV_UINT;
    j->u = v;
    return j;
}

static JsonValue *jint(long long v) {
    JsonValue *j = calloc(1, sizeof *j);
    j->type = JV_INT;
    j->i = v;
    return j;
}

static JsonValue *jfloat(double v) {
    JsonValue *j = calloc(1, sizeof *j);
    j->type = JV_FLOAT;
    j->f = v;
    return j;
}

static JsonValue *jbool(int v) {
    JsonValue *j = calloc(1, sizeof *j);
    j->type = JV_BOOL;
    j->boolean = v;
    return j;
}

static JsonValue *jnull(void) {
    JsonValue *j = calloc(1, sizeof *j);
    j->type = JV_NULL;
    return j;
}

static JsonValue *jarr(void) {
    JsonValue *j = calloc(1, sizeof *j);
    j->type = JV_ARR;
    return j;
}

static void jarr_push(JsonValue *a, JsonValue *v) {
    a->items = realloc(a->items, (a->n + 1) * sizeof(JsonValue *));
    a->items[a->n++] = v;
}

static JsonValue *jobj_new(void) {
    JsonValue *j = calloc(1, sizeof *j);
    j->type = JV_OBJ;
    return j;
}

static void jobj_put(JsonValue *o, const char *k, JsonValue *v) {
    o->members = realloc(o->members, (o->nm + 1) * sizeof(JvMember));
    o->members[o->nm].key = strdup(k);
    o->members[o->nm].val = v;
    o->nm++;
}

static void proc_swaps(char ***names, unsigned long long **used, unsigned long long **total,
                       size_t *n) {
    *names = NULL;
    *used = NULL;
    *total = NULL;
    *n = 0;
    char *text = detect_read_file("/proc/swaps");
    if (!text)
        return;
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    int first = 1;
    while (line) {
        if (first) {
            first = 0;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char *dup = strdup(line);
        char *s2 = NULL;
        char *p0 = strtok_r(dup, " \t", &s2);
        char *p1 = strtok_r(NULL, " \t", &s2);
        char *p2 = strtok_r(NULL, " \t", &s2);
        char *p3 = strtok_r(NULL, " \t", &s2);
        strtok_r(NULL, " \t", &s2);
        if (p0 && p1 && p2 && p3) {
            unsigned long long t = strtoull(p2, NULL, 10);
            unsigned long long u = strtoull(p3, NULL, 10);
            if (t > 0) {
                *names = realloc(*names, (*n + 1) * sizeof(char *));
                *used = realloc(*used, (*n + 1) * sizeof(unsigned long long));
                *total = realloc(*total, (*n + 1) * sizeof(unsigned long long));
                (*names)[*n] = strdup(p0);
                (*used)[*n] = u * 1024;
                (*total)[*n] = t * 1024;
                (*n)++;
            }
        }
        free(dup);
        line = strtok_r(NULL, "\n", &save);
    }
    free(text);
}

static void boot_time_iso(unsigned long long secs, char *out, size_t n) {
    time_t t = (time_t)secs;
    struct tm tm;
    localtime_r(&t, &tm);
    long off = tm.tm_gmtoff;
    char sign = off >= 0 ? '+' : '-';
    if (off < 0)
        off = -off;
    snprintf(out, n, "%04d-%02d-%02dT%02d:%02d:%02d.000%c%02ld%02ld", tm.tm_year + 1900,
             tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, sign,
             off / 3600, (off % 3600) / 60);
}

JsonValue *module_json_result(const char *name, const ModuleInstance *inst,
                              const JfConfig *cfg) {
    (void)cfg;
    char l[64];
    size_t i = 0;
    while (name[i] && i + 1 < sizeof l) {
        char c = name[i];
        l[i++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    l[i] = 0;
    if (!strcmp(l, "title"))
        return NULL;
    if (!strcmp(l, "os")) {
        OsInfo o;
        detect_os(&o);
        JsonValue *j = jobj_new();
        jobj_put(j, "buildID", jstr(o.build_id));
        jobj_put(j, "codename", jstr(o.codename));
        jobj_put(j, "id", jstr(o.id));
        jobj_put(j, "idLike", jstr(o.id_like));
        jobj_put(j, "name", jstr(o.name));
        jobj_put(j, "prettyName", jstr(o.pretty_name));
        jobj_put(j, "variant", jstr(o.variant));
        jobj_put(j, "variantID", jstr(o.variant_id));
        jobj_put(j, "version", jstr(o.version));
        jobj_put(j, "versionID", jstr(o.version_id));
        return j;
    }
    if (!strcmp(l, "kernel")) {
        KernelInfo k;
        detect_kernel(&k);
        char arch[32];
        detect_arch(arch, sizeof arch);
        JsonValue *j = jobj_new();
        jobj_put(j, "architecture", jstr(arch));
        jobj_put(j, "name", jstr(k.sysname));
        jobj_put(j, "release", jstr(k.release));
        jobj_put(j, "version", jstr(k.version));
        jobj_put(j, "pageSize", jint((long long)sysconf(_SC_PAGESIZE)));
        return j;
    }
    if (!strcmp(l, "wm")) {
        WmInfo w;
        detect_wm(&w);
        if (!w.name[0])
            return NULL;
        JsonValue *j = jobj_new();
        jobj_put(j, "processName", jstr(w.name));
        jobj_put(j, "prettyName", jstr(w.name));
        jobj_put(j, "protocolName", jstr(w.session_type));
        jobj_put(j, "pluginName", jstr(""));
        jobj_put(j, "version", jstr(""));
        return j;
    }
    if (!strcmp(l, "de")) {
        DeInfo d;
        detect_de(&d);
        if (!d.name[0])
            return NULL;
        JsonValue *j = jobj_new();
        jobj_put(j, "processName", jstr(d.name));
        jobj_put(j, "prettyName", jstr(d.name));
        jobj_put(j, "protocolName", jstr(""));
        jobj_put(j, "pluginName", jstr(""));
        jobj_put(j, "version", jstr(""));
        return j;
    }
    if (!strcmp(l, "initsystem")) {
        InitSystemInfo x;
        detect_initsystem(&x);
        if (!x.name[0])
            return NULL;
        JsonValue *j = jobj_new();
        jobj_put(j, "name", jstr(x.name));
        jobj_put(j, "version", jstr(x.version));
        return j;
    }
    if (!strcmp(l, "lm")) {
        LoginManagerInfo m;
        detect_lm(&m);
        if (!m.name[0])
            return NULL;
        JsonValue *j = jobj_new();
        jobj_put(j, "name", jstr(m.name));
        jobj_put(j, "version", jstr(m.version));
        return j;
    }
    if (!strcmp(l, "terminal")) {
        TerminalInfo t;
        detect_terminal(&t);
        if (!t.name[0])
            return NULL;
        char low[256];
        size_t q = 0;
        while (t.name[q] && q + 1 < sizeof low) {
            char c = t.name[q];
            low[q++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        low[q] = 0;
        ProcInfo pi;
        const char *nm = low;
        int have_pi = detect_proc_by_comm(&nm, 1, &pi);
        char exe_name[256];
        if (have_pi && pi.exe_path[0]) {
            const char *b = strrchr(pi.exe_path, '/');
            snprintf(exe_name, sizeof exe_name, "%.255s", b ? b + 1 : pi.exe_path);
        } else {
            snprintf(exe_name, sizeof exe_name, "%s", t.name);
        }
        JsonValue *j = jobj_new();
        jobj_put(j, "processName", jstr(t.name));
        jobj_put(j, "exe", jstr(have_pi ? pi.cmdline : ""));
        jobj_put(j, "exeName", jstr(exe_name));
        jobj_put(j, "exePath", jstr(have_pi ? pi.exe_path : ""));
        jobj_put(j, "pid", have_pi ? juint(pi.pid) : jnull());
        jobj_put(j, "ppid", have_pi ? juint(pi.ppid) : jnull());
        jobj_put(j, "prettyName", jstr(t.name));
        jobj_put(j, "version", jstr(t.version));
        jobj_put(j, "tty", jstr(""));
        if (have_pi)
            detect_proc_free(&pi);
        return j;
    }
    if (!strcmp(l, "shell")) {
        ShellInfo s;
        detect_shell(&s);
        if (!s.shell_name[0])
            return NULL;
        JsonValue *j = jobj_new();
        jobj_put(j, "exe", jstr(s.shell_base_name));
        jobj_put(j, "exeName", jstr(s.shell_base_name));
        jobj_put(j, "exePath", jstr(s.shell_path));
        jobj_put(j, "pid", jnull());
        jobj_put(j, "ppid", jnull());
        jobj_put(j, "processName", jstr(s.shell_base_name));
        jobj_put(j, "prettyName", jstr(s.shell_base_name));
        jobj_put(j, "version", jstr(s.shell_version));
        jobj_put(j, "tty", jint(0));
        return j;
    }
    if (!strcmp(l, "packages")) {
        PackagesInfo p;
        detect_packages(&p);
        if (p.n == 0) {
            detect_packages_free(&p);
            return NULL;
        }
        unsigned long long all = 0, nix_system = 0, nix_user = 0, flat_system = 0,
                           flat_user = 0;
        for (size_t k = 0; k < p.n; k++) {
            all += p.amounts[k].count;
            if (!strcmp(p.amounts[k].name, "nix"))
                nix_system = p.amounts[k].count;
            else if (!strcmp(p.amounts[k].name, "nix-user"))
                nix_user = p.amounts[k].count;
            else if (!strcmp(p.amounts[k].name, "flatpak"))
                flat_system = p.amounts[k].count;
            else if (!strcmp(p.amounts[k].name, "flatpak-user"))
                flat_user = p.amounts[k].count;
        }
        detect_packages_free(&p);
        JsonValue *j = jobj_new();
        jobj_put(j, "all", juint(all));
        jobj_put(j, "flatpakSystem", juint(flat_system));
        jobj_put(j, "flatpakUser", juint(flat_user));
        jobj_put(j, "nixSystem", juint(nix_system));
        jobj_put(j, "nixUser", juint(nix_user));
        return j;
    }
    if (!strcmp(l, "board")) {
        BoardInfo b;
        detect_board(&b);
        if (!b.name[0])
            return NULL;
        char serial[256] = "";
        char *t = detect_read_file("/sys/class/dmi/id/board_serial");
        if (t) {
            char *e = t + strlen(t);
            while (e > t && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
                *--e = 0;
            snprintf(serial, sizeof serial, "%s", t);
            free(t);
        }
        JsonValue *j = jobj_new();
        jobj_put(j, "name", jstr(b.name));
        jobj_put(j, "vendor", jstr(b.vendor));
        jobj_put(j, "version", jstr(b.version));
        jobj_put(j, "serial", jstr(serial));
        return j;
    }
    if (!strcmp(l, "host")) {
        char nm[256];
        detect_board_product_name(nm, sizeof nm);
        if (!nm[0])
            return NULL;
        JsonValue *j = jobj_new();
        jobj_put(j, "name", jstr(nm));
        return j;
    }
    if (!strcmp(l, "cpu")) {
        CpuInfo c;
        detect_cpu(&c);
        if (c.physical_cores == 0)
            return NULL;
        unsigned long long base = c.freq_cur_mhz ? c.freq_cur_mhz : c.freq_max_mhz;
        JsonValue *ct;
        if (c.has_pe_cores && c.has_ee_cores) {
            ct = jarr();
            JsonValue *pe = jobj_new();
            jobj_put(pe, "count", juint(c.pe_cores));
            jobj_put(pe, "freq", juint(c.freq_max_mhz));
            jarr_push(ct, pe);
            JsonValue *ee = jobj_new();
            jobj_put(ee, "count", juint(c.ee_cores));
            jobj_put(ee, "freq", juint(c.freq_max_mhz));
            jarr_push(ct, ee);
        } else {
            ct = jarr();
            JsonValue *one = jobj_new();
            jobj_put(one, "count", juint(c.logical_cores));
            jobj_put(one, "freq", juint(c.freq_max_mhz));
            jarr_push(ct, one);
        }
        char march[64] = "";
        detect_cpu_march(march, sizeof march);
        JsonValue *j = jobj_new();
        jobj_put(j, "cpu", jstr(c.model));
        jobj_put(j, "vendor", jstr(c.vendor));
        jobj_put(j, "packages", juint(c.packages));
        JsonValue *cores = jobj_new();
        jobj_put(cores, "physical", juint(c.physical_cores));
        jobj_put(cores, "logical", juint(c.logical_cores));
        jobj_put(cores, "online", juint(c.logical_cores));
        jobj_put(j, "cores", cores);
        JsonValue *freq = jobj_new();
        jobj_put(freq, "base", juint(base));
        jobj_put(freq, "max", juint(c.freq_max_mhz));
        jobj_put(j, "frequency", freq);
        jobj_put(j, "coreTypes", ct);
        jobj_put(j, "temperature", jnull());
        jobj_put(j, "march", march[0] ? jstr(march) : jnull());
        jobj_put(j, "numaNodes", juint(detect_numa_nodes()));
        jobj_put(j, "codeName", jnull());
        jobj_put(j, "technology", jnull());
        return j;
    }
    if (!strcmp(l, "gpu")) {
        size_t n = 0;
        GpuInfo *gpus = detect_gpu(&n);
        if (n == 0) {
            detect_gpu_free(gpus, n);
            return NULL;
        }
        JsonValue *a = jarr();
        for (size_t k = 0; k < n; k++) {
            JsonValue *devid;
            const char *ds = gpus[k].device_id;
            char *end = NULL;
            unsigned long v = 0;
            int ok = ds[0] != 0 && ds[0] != ' ' && ds[0] != '\t' && ds[0] != '\n';
            if (ok) {
                v = strtoul(ds, &end, 10);
                ok = end != ds && *end == 0 && v <= 4294967295UL;
            }
            if (ok)
                devid = juint(v);
            else
                devid = jnull();
            JsonValue *g = jobj_new();
            jobj_put(g, "index", jnull());
            jobj_put(g, "coreCount", jnull());
            jobj_put(g, "coreUsage", jnull());
            JsonValue *mem = jobj_new();
            JsonValue *ded = jobj_new();
            jobj_put(ded, "total", jnull());
            jobj_put(ded, "used", jnull());
            jobj_put(mem, "dedicated", ded);
            JsonValue *shr = jobj_new();
            jobj_put(shr, "total", jnull());
            jobj_put(shr, "used", jnull());
            jobj_put(mem, "shared", shr);
            jobj_put(mem, "type", jnull());
            jobj_put(g, "memory", mem);
            jobj_put(g, "driver", jstr(gpus[k].driver));
            jobj_put(g, "name", jstr(gpus[k].model));
            jobj_put(g, "temperature", jnull());
            jobj_put(g, "type", jnull());
            jobj_put(g, "vendor", jstr(gpus[k].vendor_name));
            jobj_put(g, "platformApi", jstr(""));
            jobj_put(g, "frequency", jnull());
            jobj_put(g, "deviceId", devid);
            jobj_put(g, "pcieSpeed", jnull());
            jarr_push(a, g);
        }
        detect_gpu_free(gpus, n);
        return a;
    }
    if (!strcmp(l, "display")) {
        DisplayInfo *ds = NULL;
        size_t n = 0;
        detect_display(&ds, &n);
        if (n == 0) {
            detect_display_free(ds, n);
            return NULL;
        }
        JsonValue *a = jarr();
        for (size_t k = 0; k < n; k++) {
            double scale = (ds[k].scale > 0.1 && ds[k].scale < 10.0) ? ds[k].scale : 1.0;
            unsigned long long sw = (unsigned long long)((double)ds[k].width / scale + 0.5);
            unsigned long long sh = (unsigned long long)((double)ds[k].height / scale + 0.5);
            if (sw < 1)
                sw = 1;
            if (sh < 1)
                sh = 1;
            JsonValue *d = jobj_new();
            jobj_put(d, "id", jnull());
            jobj_put(d, "name", jstr(ds[k].name));
            jobj_put(d, "primary", jbool(0));
            JsonValue *o = jobj_new();
            jobj_put(o, "width", juint(ds[k].width));
            jobj_put(o, "height", juint(ds[k].height));
            jobj_put(o, "refreshRate", jfloat((double)ds[k].refresh_rate));
            jobj_put(o, "drrStatus", jnull());
            jobj_put(o, "dpi", juint(96));
            jobj_put(d, "output", o);
            JsonValue *sc = jobj_new();
            jobj_put(sc, "width", juint(sw));
            jobj_put(sc, "height", juint(sh));
            jobj_put(d, "scaled", sc);
            JsonValue *pr = jobj_new();
            jobj_put(pr, "width", juint(ds[k].width));
            jobj_put(pr, "height", juint(ds[k].height));
            jobj_put(pr, "refreshRate", jfloat(60.0));
            jobj_put(d, "preferred", pr);
            JsonValue *ph = jobj_new();
            jobj_put(ph, "width", jnull());
            jobj_put(ph, "height", jnull());
            jobj_put(d, "physical", ph);
            jobj_put(d, "rotation", jint(0));
            jobj_put(d, "bitDepth", jnull());
            jobj_put(d, "hdrStatus", jnull());
            jobj_put(d, "type", jnull());
            JsonValue *md = jobj_new();
            jobj_put(md, "year", jnull());
            jobj_put(md, "week", jnull());
            jobj_put(d, "manufactureDate", md);
            jobj_put(d, "serial", jstr(""));
            jobj_put(d, "platformApi", jstr(""));
            jarr_push(a, d);
        }
        detect_display_free(ds, n);
        return a;
    }
    if (!strcmp(l, "disk")) {
        size_t n = 0;
        DiskInfo *disks = detect_disk(NULL, 0, &n);
        if (n == 0) {
            detect_disk_free(disks, n);
            return NULL;
        }
        JsonValue *a = jarr();
        for (size_t k = 0; k < n; k++) {
            JsonValue *vol = jarr();
            int has_sub = 0, has_ro = 0;
            char *odup = strdup(disks[k].options);
            char *save = NULL;
            char *tok = strtok_r(odup, ",", &save);
            while (tok) {
                if (!strncmp(tok, "subvol=", 7) && strcmp(disks[k].mountpoint, "/"))
                    has_sub = 1;
                if (!strcmp(tok, "ro"))
                    has_ro = 1;
                tok = strtok_r(NULL, ",", &save);
            }
            free(odup);
            if (has_sub)
                jarr_push(vol, jstr("Subvolume"));
            if (has_ro)
                jarr_push(vol, jstr("Read-only"));
            if (vol->n == 0)
                jarr_push(vol, jstr("Regular"));
            JsonValue *d = jobj_new();
            JsonValue *bytes = jobj_new();
            jobj_put(bytes, "available", juint(disks[k].available));
            jobj_put(bytes, "free",
                     juint(disks[k].total >= disks[k].used ? disks[k].total - disks[k].used : 0));
            jobj_put(bytes, "total", juint(disks[k].total));
            jobj_put(bytes, "used", juint(disks[k].used));
            jobj_put(d, "bytes", bytes);
            JsonValue *files = jobj_new();
            jobj_put(files, "total", jnull());
            jobj_put(files, "used", jnull());
            jobj_put(d, "files", files);
            jobj_put(d, "filesystem", jstr(disks[k].filesystem));
            jobj_put(d, "mountpoint", jstr(disks[k].mountpoint));
            jobj_put(d, "mountFrom", jstr(disks[k].mount_from));
            jobj_put(d, "name", jstr(disks[k].name));
            jobj_put(d, "volumeType", vol);
            jobj_put(d, "createTime", jnull());
            jarr_push(a, d);
        }
        detect_disk_free(disks, n);
        return a;
    }
    if (!strcmp(l, "memory")) {
        MemoryInfo m;
        detect_memory(&m);
        if (m.mem_total == 0)
            return NULL;
        JsonValue *j = jobj_new();
        jobj_put(j, "total", juint(m.mem_total));
        jobj_put(j, "used", juint(m.mem_used));
        return j;
    }
    if (!strcmp(l, "swap")) {
        char **names = NULL;
        unsigned long long *used = NULL, *total = NULL;
        size_t n = 0;
        proc_swaps(&names, &used, &total, &n);
        if (n == 0) {
            free(names);
            free(used);
            free(total);
            return NULL;
        }
        JsonValue *a = jarr();
        for (size_t k = 0; k < n; k++) {
            JsonValue *s = jobj_new();
            jobj_put(s, "name", jstr(names[k]));
            jobj_put(s, "used", juint(used[k]));
            jobj_put(s, "total", juint(total[k]));
            jarr_push(a, s);
            free(names[k]);
        }
        free(names);
        free(used);
        free(total);
        return a;
    }
    if (!strcmp(l, "uptime")) {
        UptimeInfo u;
        detect_uptime(&u);
        if (u.uptime_secs == 0)
            return NULL;
        char iso[64];
        boot_time_iso(u.boot_time_secs, iso, sizeof iso);
        JsonValue *j = jobj_new();
        jobj_put(j, "uptime", juint(u.uptime_secs * 1000));
        jobj_put(j, "bootTime", jstr(iso));
        return j;
    }
    if (!strcmp(l, "command")) {
        ModuleOutput *o = render_command(inst);
        if (!o || o->nvalues == 0) {
            if (o) {
                module_output_free_contents(o);
                free(o);
            }
            return NULL;
        }
        JsonValue *j = jstr(o->values[0]);
        module_output_free_contents(o);
        free(o);
        return j;
    }
    if (!strcmp(l, "battery")) {
        size_t n = 0;
        BatteryInfo *bs = detect_battery(&n);
        if (n == 0) {
            detect_battery_free(bs, n);
            return NULL;
        }
        JsonValue *a = jarr();
        for (size_t k = 0; k < n; k++) {
            JsonValue *b = jobj_new();
            jobj_put(b, "capacity", juint(bs[k].capacity_percent));
            jobj_put(b, "maxCapacity", juint(bs[k].capacity_percent));
            jobj_put(b, "percentage", juint(bs[k].capacity_percent));
            jobj_put(b, "status", jstr(bs[k].status));
            jobj_put(b, "manufacturer", jstr(bs[k].manufacturer));
            jobj_put(b, "model", jstr(bs[k].model));
            jobj_put(b, "energyNow", jfloat(bs[k].energy_now));
            jobj_put(b, "energyFull", jfloat(bs[k].energy_full));
            jobj_put(b, "temperature", jfloat(bs[k].temp_c));
            jarr_push(a, b);
        }
        detect_battery_free(bs, n);
        return a;
    }
    if (!strcmp(l, "users")) {
        size_t n = 0;
        LoggedUser *us = detect_users(&n);
        if (n == 0) {
            detect_users_free(us, n);
            return NULL;
        }
        JsonValue *a = jarr();
        for (size_t k = 0; k < n; k++) {
            JsonValue *u = jobj_new();
            jobj_put(u, "userName", jstr(us[k].user));
            jobj_put(u, "hostName", jstr(us[k].host));
            jobj_put(u, "tty", jstr(us[k].tty));
            jarr_push(a, u);
        }
        detect_users_free(us, n);
        return a;
    }
    if (!strcmp(l, "brightness")) {
        size_t n = 0;
        BrightnessInfo *bs = detect_brightness(&n);
        if (n == 0) {
            detect_brightness_free(bs, n);
            return NULL;
        }
        JsonValue *j = jobj_new();
        jobj_put(j, "name", jstr(bs[0].name));
        jobj_put(j, "value", juint(bs[0].value));
        jobj_put(j, "max", juint(bs[0].max));
        jobj_put(j, "percentage", juint(bs[0].percentage));
        detect_brightness_free(bs, n);
        return j;
    }
    if (!strcmp(l, "dns")) {
        DnsInfo d;
        if (!detect_dns(&d))
            return NULL;
        JsonValue *a = jarr();
        for (size_t k = 0; k < d.nservers; k++)
            jarr_push(a, jstr(d.servers[k]));
        detect_dns_free(&d);
        return a;
    }
    if (!strcmp(l, "localip")) {
        size_t n = 0;
        IpInfo *ips = detect_localip(&n);
        if (n == 0) {
            detect_localip_free(ips, n);
            return NULL;
        }
        JsonValue *a = jarr();
        for (size_t k = 0; k < n; k++) {
            JsonValue *o = jobj_new();
            jobj_put(o, "name", jstr(ips[k].name));
            jobj_put(o, "mac", jstr(ips[k].mac));
            JsonValue *v4 = jarr();
            for (size_t q = 0; q < ips[k].n4; q++)
                jarr_push(v4, jstr(ips[k].ipv4[q]));
            jobj_put(o, "ipv4", v4);
            JsonValue *v6 = jarr();
            for (size_t q = 0; q < ips[k].n6; q++)
                jarr_push(v6, jstr(ips[k].ipv6[q]));
            jobj_put(o, "ipv6", v6);
            jobj_put(o, "mtu", juint(ips[k].mtu));
            jobj_put(o, "flags", jstr(ips[k].flags));
            jarr_push(a, o);
        }
        detect_localip_free(ips, n);
        return a;
    }
    if (!strcmp(l, "wifi")) {
        size_t n = 0;
        WifiInfo *ws = detect_wifi(&n);
        if (n == 0) {
            detect_wifi_free(ws, n);
            return NULL;
        }
        JsonValue *a = jarr();
        for (size_t k = 0; k < n; k++) {
            JsonValue *o = jobj_new();
            jobj_put(o, "name", jstr(ws[k].name));
            jobj_put(o, "ssid", jstr(ws[k].ssid));
            jobj_put(o, "signalQuality", juint(ws[k].signal_quality));
            jobj_put(o, "protocol", jstr(ws[k].protocol));
            jarr_push(a, o);
        }
        detect_wifi_free(ws, n);
        return a;
    }
    if (!strcmp(l, "publicip")) {
        unsigned timeout = 1000;
        if (inst->raw && inst->raw->type == JV_OBJ) {
            for (size_t k = 0; k < inst->raw->nm; k++) {
                if (!strcmp(inst->raw->members[k].key, "timeout")) {
                    unsigned long long t = 0;
                    if (json_u64(inst->raw->members[k].val, &t) && t >= 100)
                        timeout = (unsigned)t;
                }
            }
        }
        char ip[64];
        if (!detect_publicip(ip, sizeof ip, timeout))
            return NULL;
        JsonValue *j = jobj_new();
        jobj_put(j, "ip", jstr(ip));
        return j;
    }
    if (!strcmp(l, "theme") || !strcmp(l, "icons") || !strcmp(l, "cursor") ||
        !strcmp(l, "font")) {
        GtkThemeInfo t;
        detect_theme(&t);
        const char *v = "";
        if (!strcmp(l, "theme"))
            v = t.gtk_theme;
        else if (!strcmp(l, "icons"))
            v = t.icon_theme;
        else if (!strcmp(l, "cursor"))
            v = t.cursor_theme;
        else
            v = t.font;
        if (!v[0])
            return NULL;
        JsonValue *j = jobj_new();
        jobj_put(j, "name", jstr(v));
        return j;
    }
    return NULL;
}
