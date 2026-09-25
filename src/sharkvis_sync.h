#ifndef JEFETCH_SHARKVIS_H
#define JEFETCH_SHARKVIS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Rgb;

typedef enum {
    SVM_AUTO,
    SVM_ON,
    SVM_OFF
} SharkvisMode;

SharkvisMode sv_mode_parse(const char *v);
int sv_mode_enabled(SharkvisMode m, int running);

float beat_speed_mult(float beat, float depth);

typedef struct {
    int active;
    int has_grad;
    Rgb glo;
    Rgb ghi;
    int has_flat;
    Rgb flat;
    char **glyphs;
    size_t nglyphs;
    float energy;
    float beat;
    float bass;
    float left;
    float right;
    float speed_mult;
} LiveFrame;

void live_frame_free_contents(LiveFrame *f);
Rgb sv_lerp_rgb(Rgb lo, Rgb hi, float t);

int sv_is_running(void);
char **sv_config_paths(size_t *n);
void sv_free_paths(char **p, size_t n);
int sv_gradient_colors(Rgb *lo, Rgb *hi);
char **sv_glyph_ramp(size_t *n);
void sv_free_strs(char **p, size_t n);
int sv_parse_color(const char *s, Rgb *out);

typedef struct {
    int has_color;
    Rgb color;
    int has_energy;
    float energy;
    int has_beat;
    float beat;
    int has_glow;
    Rgb glow;
    int has_ghigh;
    Rgb ghigh;
    int has_bass;
    float bass;
    int has_left;
    float left;
    int has_right;
    float right;
    int has_started;
    unsigned long long started;
} LiveState;

int sv_read_live_state(LiveState *out);
void sv_parse_state_text(const char *text, LiveState *out);

typedef struct Sync Sync;

Sync *sync_new(void);
void sync_free(Sync *s);
LiveFrame sync_poll(Sync *s, SharkvisMode mode, float beat_depth, int live_colors);
const LiveFrame *sync_last(const Sync *s);

int sv_is_live_color_name(const char *s);
int sv_grad_for_row(const LiveFrame *f, size_t idx, size_t total, Rgb *out);
int sv_has_display_color(const LiveFrame *f);
void sv_rgb_ansi_start(Rgb c, char *out, size_t n);
void sv_swap_placeholders(const char *s, int has_live, Rgb live, char *out, size_t n);

#endif
