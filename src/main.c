#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "app.h"
#include "logo.h"
#include "modules.h"
#include "sharkvis_sync.h"
#include "version.h"

static const char USAGE[] =
    "\nUsage: jefetch [options]\n"
    "\nOptions:\n"
    "  -h, --help                  Show this help message\n"
    "  -v, --version               Show the version number\n"
    "  -s, --structure <modules>   Set custom `module:module:module` structure\n"
    "  -l, --logo <name|path>      Override the logo (builtin id or image file)\n"
    "  -c, --config <path>         Load a custom config file\n"
    "      --no-config             Load without config file\n"
    "      --list-modules          List all available modules\n"
    "      --list-presets          List available presets\n"
    "      --list-config-paths     List search paths for config files\n"
    "      --list-data-paths       List search paths for presets and logos\n"
    "      --list-logos            List available logos\n"
    "  -j, --json                  List JSON output\n"
    "      --static                One shot static output\n";

static void list_modules(void) {
    for (size_t i = 0; i < JF_NMODULES; i++)
        printf("%zu) %-14s: %s\n", i + 1, JF_MODULES[i].name, JF_MODULES[i].desc);
}

int main(int argc, char **argv) {
    {
        system("stty sane 2>/dev/null");
        FILE *f = fopen("/dev/tty", "r");
        if (f) {
            int fd = fileno(f);
            struct termios term;
            if (tcgetattr(fd, &term) == 0) {
                struct termios orig = term;
                term.c_lflag &= (unsigned)(~(ICANON | ECHO));
                term.c_cc[VMIN] = 0;
                term.c_cc[VTIME] = 1;
                tcsetattr(fd, TCSANOW, &term);
                uint8_t buf[1024];
                for (;;) {
                    ssize_t k = read(fd, buf, sizeof buf);
                    if (k <= 0)
                        break;
                    int kitty = 0, p1r = 0;
                    for (ssize_t i = 0; i < k; i++) {
                        if (i + 11 <= k && !memcmp(buf + i, "kitty-query", 11))
                            kitty = 1;
                        if (i + 4 <= k && !memcmp(buf + i, "P1+r", 4))
                            p1r = 1;
                    }
                    if (!kitty && !p1r)
                        break;
                }
                tcsetattr(fd, TCSANOW, &orig);
            }
            fclose(f);
        }
    }
    App app;
    app_init(&app);
    CliOptions *opts = &app.options;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            printf("%s\n", USAGE);
            app_free(&app);
            return 0;
        }
        if (!strcmp(a, "-v") || !strcmp(a, "--version")) {
            jf_print_full();
            app_free(&app);
            return 0;
        }
        if (!strcmp(a, "--version-raw")) {
            printf("%s\n", JF_RAW_VERSION);
            app_free(&app);
            return 0;
        }
        if (!strcmp(a, "--list-modules")) {
            list_modules();
            app_free(&app);
            return 0;
        }
        if (!strcmp(a, "--list-logos")) {
            for (size_t k = 0; k < JF_LOGO_COUNT; k++)
                printf("%s\n", logo_name_at(k));
            app_free(&app);
            return 0;
        }
        if (!strcmp(a, "--list-config-paths")) {
            size_t n = 0;
            char **dirs = app_config_search_dirs(&n);
            for (size_t k = 0; k < n; k++)
                printf("%s\n", dirs[k]);
            app_free_strs(dirs, n);
            app_free(&app);
            return 0;
        }
        if (!strcmp(a, "--dump-term-palette")) {
            Rgb pal[16];
            if (!sv_term_palette(pal)) {
                fprintf(stderr, "terminal palette query failed\n");
                app_free(&app);
                return 1;
            }
            for (int k = 0; k < 16; k++)
                printf("%2d: #%02x%02x%02x\n", k, pal[k].r, pal[k].g, pal[k].b);
            app_free(&app);
            return 0;
        }
        if (!strcmp(a, "--no-config")) {
            opts->no_config = 1;
        } else if (!strcmp(a, "--static")) {
            opts->force_static = 1;
        } else if ((!strcmp(a, "-s") || !strcmp(a, "--structure")) && i + 1 < argc) {
            free(opts->structure);
            opts->structure = strdup(argv[++i]);
        } else if ((!strcmp(a, "-c") || !strcmp(a, "--config")) && i + 1 < argc) {
            free(opts->config_path);
            opts->config_path = strdup(argv[++i]);
        } else if ((!strcmp(a, "-l") || !strcmp(a, "--logo")) && i + 1 < argc) {
            free(opts->logo_name);
            opts->logo_name = strdup(argv[++i]);
        } else if (!strcmp(a, "-j") || !strcmp(a, "--json")) {
            opts->json = 1;
        } else {
            const char *eq = strchr(a, '=');
            if (eq) {
                size_t kl = (size_t)(eq - a);
                char k[32];
                if (kl >= sizeof k)
                    kl = sizeof k - 1;
                memcpy(k, a, kl);
                k[kl] = 0;
                const char *v = eq + 1;
                if (!strcmp(k, "-s") || !strcmp(k, "--structure")) {
                    free(opts->structure);
                    opts->structure = strdup(v);
                } else if (!strcmp(k, "-c") || !strcmp(k, "--config")) {
                    free(opts->config_path);
                    opts->config_path = strdup(v);
                } else if (!strcmp(k, "-l") || !strcmp(k, "--logo")) {
                    free(opts->logo_name);
                    opts->logo_name = strdup(v);
                } else {
                    fprintf(stderr, "Error: unknown option: %s\n", a);
                    app_free(&app);
                    return 400;
                }
            } else {
                fprintf(stderr, "Error: unknown option: %s\n", a);
                app_free(&app);
                return 400;
            }
        }
    }
    int rc = app_run(&app);
    app_free(&app);
    return rc;
}
