#ifndef JEFETCH_ANIM_H
#define JEFETCH_ANIM_H

#include <stddef.h>

#include "logo_image.h"
#include "sharkvis_sync.h"

typedef struct {
    int spin_x;
    int spin_y;
    int spin_z;
    float speed;
    int speed_set;
    float speed_x;
    float speed_y;
    float speed_z;
    float size;
    float depth;
    int depth_user_set;
    int height;
    float light_x;
    float light_y;
    float light_z;
    char **shading;
    size_t nshading;
    int flat;
    int original_glyphs;
    SharkvisMode sharkvis;
    int sharkvis_set;
    int live_colors;
    int live_term_colors;
    int text_live_colors;
    float beat_depth;
    int shading_explicit;
    int chars_set;
    int has_return_secs;
    float return_secs;
} AnimConfig;

void anim_config_default(AnimConfig *c);
void anim_config_free(AnimConfig *c);
void anim_config_from_str(AnimConfig *c, const char *s);
float anim_auto_fps(const AnimConfig *c);
unsigned long anim_frame_interval_us(const AnimConfig *c);
int anim_animation_color(const char *s, char *out, size_t n);
void anim_apply_style_chars(AnimConfig *c, const char *style, const char *chars);
void anim_default_shading(char ***out, size_t *n);

typedef struct {
    float grad_lo[3];
    float grad_hi[3];
    int has_grad;
    int has_term_pal;
    Rgb term_pal[16];
    char **shading;
    size_t nshading;
    int has_shading;
    float scale;
    float audio[3];
} RenderFx;

void render_fx_none(RenderFx *fx);
void render_fx_free(RenderFx *fx);
int render_fx_is_none(const RenderFx *fx);

typedef struct LogoCloud LogoCloud;

LogoCloud *anim_build_cloud(const ResolvedLogo *logo, const AnimConfig *config);
void anim_cloud_free(LogoCloud *c);
void anim_stereo_spin(float left, float right, float *yaw, float *pitch);
double anim_ease_to_root(double phase, float dt);

ResolvedLogo *anim_render_frame(const ResolvedLogo *logo, double frame,
                                const AnimConfig *config, size_t render_height,
                                size_t info_line_count);
ResolvedLogo *anim_render_frame_with_tint(const ResolvedLogo *logo, double frame,
                                          const AnimConfig *config,
                                          size_t render_height,
                                          size_t info_line_count,
                                          const unsigned char tint[3]);
ResolvedLogo *anim_render_frame_with_fx(const ResolvedLogo *logo, double frame,
                                        const AnimConfig *config,
                                        size_t render_height,
                                        size_t info_line_count,
                                        const RenderFx *fx);
ResolvedLogo *anim_render_cloud(LogoCloud *cloud, double frame,
                                const AnimConfig *config, size_t render_height,
                                size_t info_line_count);
ResolvedLogo *anim_render_cloud_with_fx(LogoCloud *cloud, double frame,
                                        const AnimConfig *config,
                                        size_t render_height,
                                        size_t info_line_count,
                                        const RenderFx *fx);
void anim_resolved_free(ResolvedLogo *r);

#endif
