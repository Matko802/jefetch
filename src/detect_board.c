#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detect.h"

static void read_trim(const char *path, char *out, size_t n) {
    char *t = detect_read_file(path);
    out[0] = 0;
    if (!t)
        return;
    char *e = t + strlen(t);
    while (e > t && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
        *--e = 0;
    snprintf(out, n, "%s", t);
    free(t);
}

static void dt_model(char *out, size_t n) {
    static const char *paths[] = {"/proc/device-tree/model",
                                  "/sys/firmware/devicetree/base/model"};
    out[0] = 0;
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "r");
        if (!f)
            continue;
        size_t m = fread(out, 1, n - 1, f);
        fclose(f);
        size_t k = 0;
        while (k < m && out[k])
            k++;
        out[k] = 0;
        char *s = out;
        while (*s == ' ' || *s == '\t')
            s++;
        char *e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        if (*s) {
            if (s != out)
                memmove(out, s, strlen(s) + 1);
            return;
        }
    }
    out[0] = 0;
}

void detect_board(BoardInfo *out) {
    memset(out, 0, sizeof *out);
    read_trim("/sys/class/dmi/id/board_name", out->name, sizeof out->name);
    read_trim("/sys/class/dmi/id/board_vendor", out->vendor, sizeof out->vendor);
    read_trim("/sys/class/dmi/id/board_version", out->version, sizeof out->version);
    read_trim("/sys/class/dmi/id/board_asset_tag", out->date, sizeof out->date);
    if (!out->name[0])
        dt_model(out->name, sizeof out->name);
    if (!out->vendor[0]) {
        char first[256];
        snprintf(first, sizeof first, "%s", out->name);
        char *sp = strchr(first, ' ');
        if (sp)
            *sp = 0;
        const char *v = "";
        if (!strcmp(first, "Raspberry"))
            v = "Raspberry Pi";
        else if (!strcmp(first, "NVIDIA"))
            v = "NVIDIA";
        else if (!strcmp(first, "Apple"))
            v = "Apple";
        else if (!strcmp(first, "Samsung"))
            v = "Samsung";
        else if (!strcmp(first, "Qualcomm"))
            v = "Qualcomm";
        else if (!strcmp(first, "Lenovo"))
            v = "Lenovo";
        else if (!strcmp(first, "ASUS"))
            v = "ASUS";
        else if (!strcmp(first, "Pine64"))
            v = "Pine64";
        else if (!strcmp(first, "Radxa"))
            v = "Radxa";
        else if (!strcmp(first, "Orange"))
            v = "Orange Pi";
        else if (!strcmp(first, "Banana"))
            v = "Banana Pi";
        else if (!strcmp(first, "Hardkernel"))
            v = "Hardkernel";
        else if (!strcmp(first, "SolidRun"))
            v = "SolidRun";
        snprintf(out->vendor, sizeof out->vendor, "%s", v);
    }
}

void detect_board_product_name(char *out, size_t n) {
    read_trim("/sys/class/dmi/id/product_name", out, n);
    if (!out[0])
        dt_model(out, n);
}
